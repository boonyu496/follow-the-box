#include "web/wifi_ap_supervisor.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>

#include "config/network_config.h"
#include "core/time_utils.h"
#include "telemetry/debug_console.h"
#include "web/h5_request_parser.h"

namespace followbox {
namespace {

// --- Network mode -----------------------------------------------------------
// The ESP32-S3 has a single radio shared by SoftAP and STA. Running AP+STA at
// once lets the STA (re)join scan/auth steal the radio and punch gaps in the
// SoftAP beacons, which drops attached phones ("cannot connect to this
// network"). No amount of retry tuning fully fixes that on one radio, so the
// box now has two explicit modes and defaults to the rock-solid one:
//
//   HotspotOnly (default): WIFI_AP only. No STA, no scan, no reconnect churn.
//     The hotspot on 192.168.4.1 is stable forever. Local H5 control only.
//   Link: WIFI_AP_STA. Joins the user's WiFi for the cloud / remote-control
//     link. Chosen explicitly from the H5 panel; brief hotspot wobble while
//     the STA leg negotiates is accepted in this mode.
enum class NetMode : uint8_t { HotspotOnly = 0, Link = 1 };

DNSServer g_dns_server;
WifiStore* g_wifi_store = nullptr;
LocalApiAuthCallback g_require_auth = nullptr;

// Active network mode. HotspotOnly until the user opts into Link from H5.
NetMode g_net_mode = NetMode::HotspotOnly;

// Cached provisioned SSID so /api/wifi/status never re-reads NVS per poll.
char g_sta_ssid[33] = {0};
char g_sta_password[65] = {0};
portMUX_TYPE g_wifi_mux = portMUX_INITIALIZER_UNLOCKED;uint32_t g_last_wifi_supervisor_ms = 0;
uint32_t g_sta_attempt_started_ms = 0;
uint32_t g_sta_next_retry_ms = 0;
uint32_t g_sta_retry_interval_ms = 0;
uint32_t g_sta_retry_count = 0;
uint32_t g_sta_failure_count = 0;
uint32_t g_ap_recovery_count = 0;
uint32_t g_ap_down_streak = 0;
bool g_sta_connecting = false;
int g_last_sta_status = WL_IDLE_STATUS;
bool g_dns_portal_started = false;

// Scanless-reconnect cache (Link mode only). Once the STA has joined home WiFi
// we remember the exact BSSID + primary channel so a later re-join can target
// that AP directly (WiFi.begin(ssid, pass, channel, bssid)) instead of running
// a multi-channel scan. The single AP+STA radio is only stolen for a short
// same-channel auth rather than a full scan, so the SoftAP beacon barely gaps.
// A failed targeted attempt clears the cache so the next try falls back to a
// normal scan (covers the router moving channel/BSSID).
uint8_t g_sta_bssid[6] = {0};
bool g_sta_bssid_valid = false;
int32_t g_sta_channel_cached = 0;
bool g_sta_last_attempt_scanless = false;

// Boot diagnostics (set once from runtime.begin via setBootDiag). Read-only in
// the status JSON; a plain copy under the wifi mux keeps the poll lock-tiny.
char g_boot_reset_reason[16] = {0};
uint32_t g_boot_count = 0;

constexpr char kAckOk[] = "{\"ok\":true}";
constexpr char kAckRejected[] = "{\"ok\":false}";
constexpr uint32_t kWifiSupervisorIntervalMs = 1000;
constexpr uint32_t kStaConnectWindowMs = 12000;
constexpr uint32_t kStaRetryMinMs = 30000;
constexpr uint32_t kStaRetryMaxMs = 300000;
constexpr uint16_t kDnsPort = 53;

NetMode currentNetMode() {
  portENTER_CRITICAL(&g_wifi_mux);
  const NetMode mode = g_net_mode;
  portEXIT_CRITICAL(&g_wifi_mux);
  return mode;
}

void copyStaCredentials(char* ssid, size_t ssid_size, char* password,
                        size_t password_size);

bool requireWifiRouteAuth(AsyncWebServerRequest* request) {
  return g_require_auth == nullptr || g_require_auth(request);
}

bool wifiModeHasAp(wifi_mode_t mode) {
  return mode == WIFI_AP || mode == WIFI_AP_STA;
}

void formatIp(IPAddress ip, char* out, size_t out_size) {
  if (out == nullptr || out_size == 0) {
    return;
  }
  std::snprintf(out, out_size, "%u.%u.%u.%u",
                static_cast<unsigned>(ip[0]), static_cast<unsigned>(ip[1]),
                static_cast<unsigned>(ip[2]), static_cast<unsigned>(ip[3]));
}

bool ipReady(IPAddress ip) {
  return ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0;
}

bool softApReady() {
  return wifiModeHasAp(WiFi.getMode()) && ipReady(WiFi.softAPIP());
}

// The single AP+STA radio forces the SoftAP onto the STA's channel whenever the
// STA is connected. If the SoftAP is (re)started on a *different* configured
// channel, the SDK yanks it to the STA channel and kicks every associated
// client (and leaves stale saved-channel profiles that fail to reconnect ->
// "cannot connect to this network"). So once we have learned the router/STA
// channel, always bring the SoftAP up on that same channel to keep AP+STA
// aligned and stop the channel from hopping.
uint8_t preferredApChannel() {
  int32_t cached = 0;
  portENTER_CRITICAL(&g_wifi_mux);
  cached = g_sta_channel_cached;
  portEXIT_CRITICAL(&g_wifi_mux);
  if (cached >= 1 && cached <= 13) {
    return static_cast<uint8_t>(cached);
  }
  return net::SOFT_AP_CHANNEL;
}

void ensureDnsPortal(const char* reason) {
  if (!softApReady()) {
    return;
  }
  if (g_dns_portal_started) {
    return;
  }
  const bool ok = g_dns_server.start(kDnsPort, "*", WiFi.softAPIP());
  g_dns_portal_started = ok;
  FB_LOGI("wifi_ap: dns_portal reason=%s ok=%d ip=%s",
          reason != nullptr ? reason : "unknown", ok ? 1 : 0,
          WiFi.softAPIP().toString().c_str());
}

void logWifiStatus(const char* reason) {
  char ap_ip[16];
  char sta_ip[16];
  char sta_ssid[sizeof(g_sta_ssid)];
  char sta_password[sizeof(g_sta_password)];
  formatIp(WiFi.softAPIP(), ap_ip, sizeof(ap_ip));
  formatIp(WiFi.localIP(), sta_ip, sizeof(sta_ip));
  copyStaCredentials(sta_ssid, sizeof(sta_ssid), sta_password,
                     sizeof(sta_password));
  const wifi_mode_t mode = WiFi.getMode();
  const int sta_status = static_cast<int>(WiFi.status());
  const bool ap_ready = softApReady();
  const int rssi = sta_status == WL_CONNECTED ? static_cast<int>(WiFi.RSSI()) : 0;
  FB_LOGI("wifi: %s mode=%d channel=%d ap=%d ap_ip=%s ap_clients=%u sta=%d sta_ip=%s rssi=%d ssid=%s",
          reason != nullptr ? reason : "status", static_cast<int>(mode),
          static_cast<int>(WiFi.channel()), ap_ready ? 1 : 0, ap_ip,
          static_cast<unsigned>(WiFi.softAPgetStationNum()), sta_status, sta_ip,
          rssi, sta_ssid[0] != '\0' ? sta_ssid : "-");
}

bool startSoftAp(const char* reason) {
  // Ensure the radio is in an AP-capable mode. HotspotOnly uses WIFI_AP (no STA
  // = no radio contention); Link uses WIFI_AP_STA so the cloud leg can run.
  const wifi_mode_t want =
      currentNetMode() == NetMode::Link ? WIFI_AP_STA : WIFI_AP;
  if (WiFi.getMode() != want) {
    WiFi.mode(want);
  }
  const IPAddress ap_ip(192, 168, 4, 1);
  const IPAddress gateway(192, 168, 4, 1);
  const IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(ap_ip, gateway, subnet);
  const uint8_t ap_channel = preferredApChannel();
  const bool ok = WiFi.softAP(net::SOFT_AP_SSID, net::SOFT_AP_PASSWORD,
                              ap_channel, /*ssid_hidden=*/0,
                              net::SOFT_AP_MAX_CONN);
  FB_LOGI("wifi_ap: start reason=%s ok=%d ssid=%s channel=%u max_conn=%u",
          reason != nullptr ? reason : "unknown", ok ? 1 : 0,
          net::SOFT_AP_SSID, static_cast<unsigned>(ap_channel),
          static_cast<unsigned>(net::SOFT_AP_MAX_CONN));
  if (ok) {
    ensureDnsPortal(reason);
  } else {
    g_dns_portal_started = false;
  }
  logWifiStatus(reason);
  return ok;
}

uint32_t staRetryBackoffMs() {
  const uint32_t failures = g_sta_failure_count > 5 ? 5 : g_sta_failure_count;
  uint32_t interval = kStaRetryMinMs;
  for (uint32_t i = 1; i < failures; ++i) {
    interval *= 2;
  }
  return interval > kStaRetryMaxMs ? kStaRetryMaxMs : interval;
}

bool timeReached(uint32_t now_ms, uint32_t target_ms) {
  return target_ms == 0 ||
         static_cast<int32_t>(now_ms - target_ms) >= 0;
}

void cacheStaCredentials(const WifiCredentials& creds) {
  portENTER_CRITICAL(&g_wifi_mux);
  std::snprintf(g_sta_ssid, sizeof(g_sta_ssid), "%s", creds.ssid);
  std::snprintf(g_sta_password, sizeof(g_sta_password), "%s", creds.password);
  portEXIT_CRITICAL(&g_wifi_mux);
}

void copyStaCredentials(char* ssid, size_t ssid_size, char* password,
                        size_t password_size) {
  if (ssid == nullptr || password == nullptr || ssid_size == 0 ||
      password_size == 0) {
    return;
  }
  portENTER_CRITICAL(&g_wifi_mux);
  std::snprintf(ssid, ssid_size, "%s", g_sta_ssid);
  std::snprintf(password, password_size, "%s", g_sta_password);
  portEXIT_CRITICAL(&g_wifi_mux);
}

void beginStaJoin(uint32_t now_ms, const char* reason) {
  char ssid[sizeof(g_sta_ssid)];
  char password[sizeof(g_sta_password)];
  copyStaCredentials(ssid, sizeof(ssid), password, sizeof(password));
  if (ssid[0] == '\0') {
    return;
  }

  if (!wifiModeHasAp(WiFi.getMode())) {
    WiFi.mode(WIFI_AP_STA);
  }
  // Keep control of retries in this supervisor. The SDK auto-reconnect loop
  // can scan continuously on wrong/weak STA credentials and make the SoftAP
  // look like it is dropping.
  WiFi.setAutoReconnect(false);

  // Prefer a scanless, same-channel re-join when we have a cached BSSID: this
  // avoids the full multi-channel scan that would otherwise steal the shared
  // radio long enough to punch a SoftAP beacon gap and drop attached phones.
  uint8_t bssid[6];
  bool scanless = false;
  int32_t channel = 0;
  portENTER_CRITICAL(&g_wifi_mux);
  scanless = g_sta_bssid_valid && g_sta_channel_cached > 0;
  if (scanless) {
    std::memcpy(bssid, g_sta_bssid, sizeof(bssid));
    channel = g_sta_channel_cached;
  }
  g_sta_last_attempt_scanless = scanless;
  portEXIT_CRITICAL(&g_wifi_mux);

  if (scanless) {
    WiFi.begin(ssid, password, channel, bssid);
  } else {
    WiFi.begin(ssid, password);
  }

  portENTER_CRITICAL(&g_wifi_mux);
  g_sta_connecting = true;
  g_sta_attempt_started_ms = now_ms;
  g_sta_next_retry_ms = 0;
  ++g_sta_retry_count;
  const uint32_t retry_count = g_sta_retry_count;
  portEXIT_CRITICAL(&g_wifi_mux);
  FB_LOGI("wifi_sta: begin reason=%s ssid=%s retry=%lu scanless=%d channel=%ld",
          reason != nullptr ? reason : "unknown", ssid,
          static_cast<unsigned long>(retry_count), scanless ? 1 : 0,
          static_cast<long>(channel));
}

// Switch the radio between HotspotOnly (WIFI_AP) and Link (WIFI_AP_STA). Owns
// every WiFi.mode() transition so the supervisor recovery paths only ever
// (re)start the SoftAP within the already-selected mode. Pure transport: never
// touches the motion/safety path.
void applyNetMode(NetMode mode, const char* reason) {
  portENTER_CRITICAL(&g_wifi_mux);
  g_net_mode = mode;
  // A mode change invalidates any in-flight STA attempt and scanless cache.
  g_sta_connecting = false;
  g_sta_failure_count = 0;
  g_sta_retry_interval_ms = 0;
  g_sta_next_retry_ms = 0;
  g_sta_last_attempt_scanless = false;
  portEXIT_CRITICAL(&g_wifi_mux);

  if (mode == NetMode::HotspotOnly) {
    // Drop the STA leg entirely so the single radio stops contending with the
    // SoftAP. Clear the cached STA channel so the AP returns to its stable
    // configured channel instead of staying pinned to the old router channel.
    WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/false);
    portENTER_CRITICAL(&g_wifi_mux);
    g_sta_bssid_valid = false;
    g_sta_channel_cached = 0;
    portEXIT_CRITICAL(&g_wifi_mux);
    WiFi.mode(WIFI_AP);
    startSoftAp(reason);
    FB_LOGI("wifi: mode=hotspot reason=%s", reason != nullptr ? reason : "-");
    return;
  }

  // Link mode: bring up AP+STA and start joining saved credentials (if any).
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(false);
  startSoftAp(reason);
  char ssid[sizeof(g_sta_ssid)];
  char password[sizeof(g_sta_password)];
  copyStaCredentials(ssid, sizeof(ssid), password, sizeof(password));
  if (ssid[0] != '\0') {
    beginStaJoin(millis(), reason);
  }
  FB_LOGI("wifi: mode=link reason=%s ssid=%s", reason != nullptr ? reason : "-",
          ssid[0] != '\0' ? ssid : "-");
}

void wifiSupervisor(uint32_t now_ms) {
  if (elapsedMs(now_ms, g_last_wifi_supervisor_ms) < kWifiSupervisorIntervalMs) {
    return;
  }
  g_last_wifi_supervisor_ms = now_ms;

  // Debounce AP recovery. A single transient softApReady()==false reading can
  // happen while the shared radio briefly retunes; re-calling WiFi.softAP() on
  // that blip would needlessly kick every attached phone. Only rebuild the AP
  // after the down state persists across checks. This runs in both modes so the
  // hotspot is always self-healing.
  if (!softApReady()) {
    ++g_ap_down_streak;
    if (g_ap_down_streak >= 2) {
      ++g_ap_recovery_count;
      startSoftAp("supervisor-ap-recover");
      g_ap_down_streak = 0;
    }
  } else {
    g_ap_down_streak = 0;
  }

  // HotspotOnly: no STA leg at all, so there is nothing else to supervise. The
  // single radio serves only the SoftAP -> zero contention, rock-solid hotspot.
  if (currentNetMode() != NetMode::Link) {
    return;
  }

  const int sta_status = static_cast<int>(WiFi.status());
  if (sta_status != g_last_sta_status) {
    g_last_sta_status = sta_status;
    logWifiStatus("sta-status-change");
  }

  char ssid[sizeof(g_sta_ssid)];
  char password[sizeof(g_sta_password)];
  copyStaCredentials(ssid, sizeof(ssid), password, sizeof(password));
  if (ssid[0] == '\0') {
    return;
  }

  if (sta_status == WL_CONNECTED) {
    // Cache the joined AP's BSSID + channel so future re-joins can skip the
    // full scan (see beginStaJoin). Read the SDK values before taking the lock
    // so the critical section stays a plain copy.
    uint8_t bssid_copy[6] = {0};
    bool have_bssid = false;
    const uint8_t* bssid = WiFi.BSSID();
    if (bssid != nullptr) {
      std::memcpy(bssid_copy, bssid, sizeof(bssid_copy));
      have_bssid = true;
    }
    const int32_t channel = WiFi.channel();
    portENTER_CRITICAL(&g_wifi_mux);
    if (have_bssid) {
      std::memcpy(g_sta_bssid, bssid_copy, sizeof(g_sta_bssid));
      g_sta_bssid_valid = true;
    }
    if (channel > 0) {
      g_sta_channel_cached = channel;
    }
    g_sta_connecting = false;
    g_sta_failure_count = 0;
    g_sta_retry_interval_ms = 0;
    g_sta_next_retry_ms = 0;
    portEXIT_CRITICAL(&g_wifi_mux);
    return;
  }

  bool timed_out = false;
  portENTER_CRITICAL(&g_wifi_mux);
  timed_out = g_sta_connecting &&
              elapsedMs(now_ms, g_sta_attempt_started_ms) >= kStaConnectWindowMs;
  portEXIT_CRITICAL(&g_wifi_mux);
  if (timed_out) {
    WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/false);
    if (!wifiModeHasAp(WiFi.getMode())) {
      WiFi.mode(WIFI_AP_STA);
    }
    if (!softApReady()) {
      ++g_ap_recovery_count;
      startSoftAp("sta-timeout-ap-recover");
    }

    portENTER_CRITICAL(&g_wifi_mux);
    g_sta_connecting = false;
    ++g_sta_failure_count;
    // A failed scanless attempt means the cached BSSID/channel is stale (router
    // rebooted, roamed, or moved channel); drop it so the next try does a full
    // scan again.
    if (g_sta_last_attempt_scanless) {
      g_sta_bssid_valid = false;
      g_sta_channel_cached = 0;
    }
    g_sta_retry_interval_ms = staRetryBackoffMs();
    g_sta_next_retry_ms = now_ms + g_sta_retry_interval_ms;
    portEXIT_CRITICAL(&g_wifi_mux);
    FB_LOGW("wifi_sta: timeout status=%d failures=%lu next_retry_ms=%lu",
            sta_status, static_cast<unsigned long>(g_sta_failure_count),
            static_cast<unsigned long>(g_sta_retry_interval_ms));
    return;
  }

  bool should_retry = false;
  bool connecting = false;
  portENTER_CRITICAL(&g_wifi_mux);
  connecting = g_sta_connecting;
  should_retry = !connecting && timeReached(now_ms, g_sta_next_retry_ms);
  portEXIT_CRITICAL(&g_wifi_mux);

  if (!should_retry) {
    return;
  }

  // Link mode is an explicit user opt-in for the cloud / remote-control link,
  // so we retry the STA join here even while a phone is on the hotspot (that is
  // exactly when the user wants the uplink). The scanless same-channel re-join
  // in beginStaJoin keeps the SoftAP beacon gap small. Users who want a
  // never-wobble hotspot stay in HotspotOnly, which never reaches this code.
  beginStaJoin(now_ms, "supervisor-retry");
}

template <typename Fn>
void onWifiBody(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                size_t index, size_t total, Fn fn) {
  static constexpr size_t kMaxBody = 256;
  static char buf[kMaxBody + 1];
  if (total > kMaxBody) {
    request->send(413, "application/json", kAckRejected);
    return;
  }
  if (index + len > kMaxBody) {
    return;
  }
  memcpy(buf + index, data, len);
  if (index + len == total) {
    buf[total] = '\0';
    fn(request, buf, total);
  }
}

void resetStaRetryState() {
  portENTER_CRITICAL(&g_wifi_mux);
  g_sta_connecting = false;
  g_sta_failure_count = 0;
  g_sta_retry_interval_ms = 0;
  g_sta_next_retry_ms = 0;
  // New credentials target a possibly different AP; drop the scanless cache so
  // the first join discovers the correct BSSID/channel via a full scan.
  g_sta_bssid_valid = false;
  g_sta_channel_cached = 0;
  g_sta_last_attempt_scanless = false;
  portEXIT_CRITICAL(&g_wifi_mux);
}

void sendWifiStatus(AsyncWebServerRequest* request) {
  char sta_ip[16];
  char ap_ip[16];
  char sta_ssid[sizeof(g_sta_ssid)];
  char sta_password[sizeof(g_sta_password)];
  formatIp(WiFi.localIP(), sta_ip, sizeof(sta_ip));
  formatIp(WiFi.softAPIP(), ap_ip, sizeof(ap_ip));
  copyStaCredentials(sta_ssid, sizeof(sta_ssid), sta_password,
                     sizeof(sta_password));
  const bool connected = WiFi.status() == WL_CONNECTED;
  const bool ap_ready = softApReady();
  uint32_t retry_count = 0;
  uint32_t failure_count = 0;
  uint32_t retry_in_ms = 0;
  uint32_t retry_interval_ms = 0;
  uint32_t ap_recovery_count = 0;
  bool sta_connecting = false;
  NetMode net_mode = NetMode::HotspotOnly;
  char boot_reason[sizeof(g_boot_reset_reason)];
  uint32_t boot_count = 0;
  portENTER_CRITICAL(&g_wifi_mux);
  retry_count = g_sta_retry_count;
  failure_count = g_sta_failure_count;
  retry_interval_ms = g_sta_retry_interval_ms;
  ap_recovery_count = g_ap_recovery_count;
  sta_connecting = g_sta_connecting;
  net_mode = g_net_mode;
  std::snprintf(boot_reason, sizeof(boot_reason), "%s", g_boot_reset_reason);
  boot_count = g_boot_count;
  if (g_sta_next_retry_ms != 0) {
    const uint32_t now = millis();
    retry_in_ms = timeReached(now, g_sta_next_retry_ms)
                      ? 0
                      : static_cast<uint32_t>(g_sta_next_retry_ms - now);
  }
  portEXIT_CRITICAL(&g_wifi_mux);

  const bool link_mode = net_mode == NetMode::Link;
  // static: sendWifiStatus runs on the single async_tcp task; request->send()
  // copies the payload, so this 800 B scratch is safe off the task stack and
  // reduces async_tcp stack pressure (see h5_web_server /api/state).
  static char buf[800];
  std::snprintf(buf, sizeof(buf),
                "{\"ok\":true,\"net_mode\":\"%s\",\"link_enabled\":%s,"
                "\"provisioned\":%s,\"sta_connected\":%s,"
                "\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"sta_status\":%d,"
                "\"ap_ready\":%s,\"ap_ssid\":\"%s\",\"ap_ip\":\"%s\","
                "\"ap_clients\":%u,\"wifi_mode\":%d,\"wifi_channel\":%d,"
                "\"ap_config_channel\":%u,\"sta_connecting\":%s,"
                "\"sta_retry_count\":%lu,\"sta_failures\":%lu,"
                "\"sta_retry_in_ms\":%lu,\"sta_retry_interval_ms\":%lu,"
                "\"ap_recoveries\":%lu,\"reset_reason\":\"%s\","
                "\"boot_count\":%lu}",
                link_mode ? "link" : "hotspot",
                link_mode ? "true" : "false",
                sta_ssid[0] != '\0' ? "true" : "false",
                connected ? "true" : "false", sta_ssid,
                connected ? sta_ip : "",
                connected ? static_cast<int>(WiFi.RSSI()) : 0,
                static_cast<int>(WiFi.status()),
                ap_ready ? "true" : "false", net::SOFT_AP_SSID, ap_ip,
                static_cast<unsigned>(WiFi.softAPgetStationNum()),
                static_cast<int>(WiFi.getMode()), static_cast<int>(WiFi.channel()),
                static_cast<unsigned>(net::SOFT_AP_CHANNEL),
                sta_connecting ? "true" : "false",
                static_cast<unsigned long>(retry_count),
                static_cast<unsigned long>(failure_count),
                static_cast<unsigned long>(retry_in_ms),
                static_cast<unsigned long>(retry_interval_ms),
                static_cast<unsigned long>(ap_recovery_count),
                boot_reason[0] != '\0' ? boot_reason : "?",
                static_cast<unsigned long>(boot_count));
  request->send(200, "application/json", buf);
}

}  // namespace

void setBootDiag(const char* reset_reason, uint32_t boot_count) {
  portENTER_CRITICAL(&g_wifi_mux);
  std::snprintf(g_boot_reset_reason, sizeof(g_boot_reset_reason), "%s",
                reset_reason != nullptr ? reset_reason : "?");
  g_boot_count = boot_count;
  portEXIT_CRITICAL(&g_wifi_mux);
}

void beginWifiApSupervisor(WifiStore* wifi_store) {
  g_wifi_store = wifi_store;

  // Two explicit network modes on the single ESP32-S3 radio (see NetMode):
  //   HotspotOnly (default): WIFI_AP only -> the 192.168.4.1 hotspot is always
  //     available and never wobbles because no STA leg ever contends for the
  //     radio. This is where the box boots unless the user opted into Link.
  //   Link: WIFI_AP_STA -> also joins the user's WiFi for the cloud /
  //     remote-control link. Only entered at boot when the user previously
  //     enabled it AND credentials exist.
  WiFi.persistent(false);  // creds live in our own NVS namespace, not the SDK's
  WiFi.setSleep(false);    // keep AP beacons steady.
  // Cap TX power below the 19.5 dBm max. On USB-only / marginal supplies the
  // peak current of full-power transmits can brown out the ESP32-S3, which
  // resets the radio and makes the SoftAP flap. 15 dBm keeps ample range while
  // cutting the current spikes that kill the AP.
  WiFi.setTxPower(WIFI_POWER_15dBm);

  // Resolve the boot mode. Load saved credentials first; Link is only honored
  // when the user enabled it and there is something to join.
  WifiCredentials creds;
  if (g_wifi_store != nullptr) {
    creds = g_wifi_store->load();
  }
  if (!creds.valid() && net::STA_SSID[0] != '\0') {
    // Compile-time fallback for bench builds (FOLLOWBOX_WIFI_STA legacy path).
    std::snprintf(creds.ssid, sizeof(creds.ssid), "%s", net::STA_SSID);
    std::snprintf(creds.password, sizeof(creds.password), "%s",
                  net::STA_PASSWORD);
    if (g_wifi_store != nullptr) {
      g_wifi_store->save(creds);
    }
  }
  if (creds.valid()) {
    cacheStaCredentials(creds);
  }

  bool link_enabled = false;
  if (g_wifi_store != nullptr) {
    link_enabled = g_wifi_store->loadLinkEnabled();
  }
  const NetMode boot_mode = (link_enabled && creds.valid()) ? NetMode::Link
                                                            : NetMode::HotspotOnly;
  applyNetMode(boot_mode, "boot");
}

void registerWifiApSupervisorRoutes(AsyncWebServer& server,
                                    LocalApiAuthCallback require_auth) {
  g_require_auth = require_auth;

  // Phones often re-run OS connectivity probes as soon as a browser opens a
  // no-internet SoftAP. Answer the common probe URLs locally so the client
  // stays associated with FollowBox instead of auto-switching away.
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(204, "text/plain", "");
  });
  server.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(204, "text/plain", "");
  });
  server.on("/connectivity-check.html", HTTP_GET,
            [](AsyncWebServerRequest* request) {
              request->send(204, "text/plain", "");
            });
  server.on("/hotspot-detect.html", HTTP_GET,
            [](AsyncWebServerRequest* request) {
              request->send(200, "text/html", "Success");
            });
  server.on("/library/test/success.html", HTTP_GET,
            [](AsyncWebServerRequest* request) {
              request->send(200, "text/html", "Success");
            });
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", "Microsoft Connect Test");
  });
  server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", "Microsoft NCSI");
  });
  server.on("/success.txt", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/plain", "success");
  });
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(204, "image/x-icon", "");
  });

  // POST /api/wifi/mode -> switch between the stable hotspot-only mode and the
  // cloud/remote-control link mode. Register this before /api/wifi because
  // ESPAsyncWebServer can match the shorter route first.
  server.on(
      "/api/wifi/mode", HTTP_POST, [](AsyncWebServerRequest* request) {},
      nullptr,
      [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index,
         size_t total) {
        onWifiBody(request, data, len, index, total,
                   [](AsyncWebServerRequest* req, const char* body,
                      size_t length) {
                     if (!requireWifiRouteAuth(req)) {
                       return;
                     }
                     // Accept both strict JSON ({"mode":"link"}) and the
                     // quote-stripped form some CLI clients produce. The only
                     // valid values remain link/hotspot.
                     const bool want_link = std::strstr(body, "link") != nullptr;
                     const bool want_hotspot =
                         std::strstr(body, "hotspot") != nullptr;
                     if (want_link == want_hotspot) {
                       // Neither or both -> malformed request.
                       req->send(400, "application/json", kAckRejected);
                       return;
                     }
                     if (want_link) {
                       // Link mode needs credentials to join; without them the
                       // switch would just leave the STA leg idle.
                       char ssid[sizeof(g_sta_ssid)];
                       char password[sizeof(g_sta_password)];
                       copyStaCredentials(ssid, sizeof(ssid), password,
                                          sizeof(password));
                       if (ssid[0] == '\0') {
                         req->send(409, "application/json",
                                   "{\"ok\":false,\"error\":\"no_wifi\"}");
                         return;
                       }
                     }
                     if (g_wifi_store != nullptr) {
                       g_wifi_store->saveLinkEnabled(want_link);
                     }
                     // Ack before switching: applyNetMode may briefly retune the
                     // radio, so the panel gets its reply first.
                     req->send(200, "application/json", kAckOk);
                     applyNetMode(want_link ? NetMode::Link : NetMode::HotspotOnly,
                                  "api-mode");
                   });
      });

  // POST /api/wifi -> persist STA credentials and (re)join. Pure transport:
  // touches WiFi/NVS only, never the motion path. The AP stays up regardless,
  // so a wrong password can always be corrected from the hotspot.
  server.on(
      "/api/wifi", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
      [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index,
         size_t total) {
        onWifiBody(request, data, len, index, total,
                   [](AsyncWebServerRequest* req, const char* body,
                      size_t length) {
                     if (!requireWifiRouteAuth(req)) {
                       return;
                     }
                     const WifiConfigRequest wifi =
                         parseWifiConfigRequest(body, length);
                     if (!wifi.valid || g_wifi_store == nullptr) {
                       req->send(400, "application/json", kAckRejected);
                       return;
                     }
                     WifiCredentials creds;
                     std::snprintf(creds.ssid, sizeof(creds.ssid), "%s",
                                   wifi.ssid);
                     std::snprintf(creds.password, sizeof(creds.password), "%s",
                                   wifi.password);
                     const bool saved = g_wifi_store->save(creds);
                     // Reply before rejoining so the panel (often on the AP leg)
                     // gets its ack even if the STA leg churns the radio.
                     req->send(200, "application/json",
                               saved ? kAckOk : kAckRejected);
                     if (saved) {
                       cacheStaCredentials(creds);
                       // Only (re)join immediately when the box is already in
                       // Link mode. In HotspotOnly we just persist the creds so
                       // they are ready the next time the user enables Link,
                       // and the stable hotspot is never disturbed by a join.
                       if (currentNetMode() == NetMode::Link) {
                         resetStaRetryState();
                         beginStaJoin(millis(), "api-wifi");
                         logWifiStatus("sta-rejoin");
                       }
                     }
                   });
      });

  // GET /api/wifi/status -> STA join state for the provisioning UI.
  server.on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    sendWifiStatus(request);
  });
}

void loopWifiApSupervisor(uint32_t now_ms) {
  if (g_dns_portal_started) {
    g_dns_server.processNextRequest();
  } else {
    ensureDnsPortal("comm-loop");
  }
  wifiSupervisor(now_ms);
}

}  // namespace followbox
