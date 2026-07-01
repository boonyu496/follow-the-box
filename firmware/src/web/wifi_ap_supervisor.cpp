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

DNSServer g_dns_server;
WifiStore* g_wifi_store = nullptr;
LocalApiAuthCallback g_require_auth = nullptr;

// Cached provisioned SSID so /api/wifi/status never re-reads NVS per poll.
char g_sta_ssid[33] = {0};
char g_sta_password[65] = {0};
portMUX_TYPE g_wifi_mux = portMUX_INITIALIZER_UNLOCKED;
uint32_t g_last_wifi_supervisor_ms = 0;
uint32_t g_sta_attempt_started_ms = 0;
uint32_t g_sta_next_retry_ms = 0;
uint32_t g_sta_retry_interval_ms = 0;
uint32_t g_sta_retry_count = 0;
uint32_t g_sta_failure_count = 0;
uint32_t g_ap_recovery_count = 0;
bool g_sta_connecting = false;
int g_last_sta_status = WL_IDLE_STATUS;
bool g_dns_portal_started = false;

constexpr char kAckOk[] = "{\"ok\":true}";
constexpr char kAckRejected[] = "{\"ok\":false}";
constexpr uint32_t kWifiSupervisorIntervalMs = 1000;
constexpr uint32_t kStaConnectWindowMs = 12000;
constexpr uint32_t kStaRetryMinMs = 30000;
constexpr uint32_t kStaRetryMaxMs = 300000;
constexpr uint32_t kStaRetryDeferWithApClientMs = 60000;
constexpr uint16_t kDnsPort = 53;

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
  if (!wifiModeHasAp(WiFi.getMode())) {
    WiFi.mode(WIFI_AP_STA);
  }
  const IPAddress ap_ip(192, 168, 4, 1);
  const IPAddress gateway(192, 168, 4, 1);
  const IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(ap_ip, gateway, subnet);
  const bool ok = WiFi.softAP(net::SOFT_AP_SSID, net::SOFT_AP_PASSWORD,
                              net::SOFT_AP_CHANNEL, /*ssid_hidden=*/0,
                              net::SOFT_AP_MAX_CONN);
  FB_LOGI("wifi_ap: start reason=%s ok=%d ssid=%s channel=%u max_conn=%u",
          reason != nullptr ? reason : "unknown", ok ? 1 : 0,
          net::SOFT_AP_SSID, static_cast<unsigned>(net::SOFT_AP_CHANNEL),
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
  WiFi.begin(ssid, password);

  portENTER_CRITICAL(&g_wifi_mux);
  g_sta_connecting = true;
  g_sta_attempt_started_ms = now_ms;
  g_sta_next_retry_ms = 0;
  ++g_sta_retry_count;
  const uint32_t retry_count = g_sta_retry_count;
  portEXIT_CRITICAL(&g_wifi_mux);
  FB_LOGI("wifi_sta: begin reason=%s ssid=%s retry=%lu",
          reason != nullptr ? reason : "unknown", ssid,
          static_cast<unsigned long>(retry_count));
}

void wifiSupervisor(uint32_t now_ms) {
  if (elapsedMs(now_ms, g_last_wifi_supervisor_ms) < kWifiSupervisorIntervalMs) {
    return;
  }
  g_last_wifi_supervisor_ms = now_ms;

  if (!softApReady()) {
    ++g_ap_recovery_count;
    startSoftAp("supervisor-ap-recover");
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
    portENTER_CRITICAL(&g_wifi_mux);
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
  uint32_t failures = 0;
  portENTER_CRITICAL(&g_wifi_mux);
  connecting = g_sta_connecting;
  failures = g_sta_failure_count;
  should_retry = !connecting && timeReached(now_ms, g_sta_next_retry_ms);
  portEXIT_CRITICAL(&g_wifi_mux);

  if (!should_retry) {
    return;
  }

  if (failures >= 2 && WiFi.softAPgetStationNum() > 0) {
    portENTER_CRITICAL(&g_wifi_mux);
    g_sta_next_retry_ms = now_ms + kStaRetryDeferWithApClientMs;
    portEXIT_CRITICAL(&g_wifi_mux);
    FB_LOGW("wifi_sta: retry deferred, ap_clients=%u",
            static_cast<unsigned>(WiFi.softAPgetStationNum()));
    return;
  }

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
  portENTER_CRITICAL(&g_wifi_mux);
  retry_count = g_sta_retry_count;
  failure_count = g_sta_failure_count;
  retry_interval_ms = g_sta_retry_interval_ms;
  ap_recovery_count = g_ap_recovery_count;
  sta_connecting = g_sta_connecting;
  if (g_sta_next_retry_ms != 0) {
    const uint32_t now = millis();
    retry_in_ms = timeReached(now, g_sta_next_retry_ms)
                      ? 0
                      : static_cast<uint32_t>(g_sta_next_retry_ms - now);
  }
  portEXIT_CRITICAL(&g_wifi_mux);

  char buf[640];
  std::snprintf(buf, sizeof(buf),
                "{\"ok\":true,\"provisioned\":%s,\"sta_connected\":%s,"
                "\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"sta_status\":%d,"
                "\"ap_ready\":%s,\"ap_ssid\":\"%s\",\"ap_ip\":\"%s\","
                "\"ap_clients\":%u,\"wifi_mode\":%d,\"wifi_channel\":%d,"
                "\"ap_config_channel\":%u,\"sta_connecting\":%s,"
                "\"sta_retry_count\":%lu,\"sta_failures\":%lu,"
                "\"sta_retry_in_ms\":%lu,\"sta_retry_interval_ms\":%lu,"
                "\"ap_recoveries\":%lu}",
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
                static_cast<unsigned long>(ap_recovery_count));
  request->send(200, "application/json", buf);
}

}  // namespace

void beginWifiApSupervisor(WifiStore* wifi_store) {
  g_wifi_store = wifi_store;

  // AP + STA simultaneously: the AP is the always-available provisioning and
  // local-control channel (the box can never become unreachable after a bad
  // OTA/config), while the STA leg joins the user's WiFi for the cloud link
  // once credentials have been provisioned via POST /api/wifi.
  WiFi.persistent(false);  // creds live in our own NVS namespace, not the SDK's
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);  // keep AP beacons steady while STA/cloud is active.
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  startSoftAp("boot");
  WiFi.setAutoReconnect(false);

  WifiCredentials creds;
  if (g_wifi_store != nullptr) {
    creds = g_wifi_store->load();
  }
  if (creds.valid()) {
    cacheStaCredentials(creds);
    beginStaJoin(millis(), "boot-saved");
  } else if (net::STA_SSID[0] != '\0') {
    // Compile-time fallback for bench builds (FOLLOWBOX_WIFI_STA legacy path).
    if (g_wifi_store != nullptr) {
      WifiCredentials fallback;
      std::snprintf(fallback.ssid, sizeof(fallback.ssid), "%s", net::STA_SSID);
      std::snprintf(fallback.password, sizeof(fallback.password), "%s",
                    net::STA_PASSWORD);
      g_wifi_store->save(fallback);
      creds = fallback;
    } else {
      std::snprintf(creds.ssid, sizeof(creds.ssid), "%s", net::STA_SSID);
      std::snprintf(creds.password, sizeof(creds.password), "%s",
                    net::STA_PASSWORD);
    }
    cacheStaCredentials(creds);
    beginStaJoin(millis(), "boot-build-flags");
  }
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
                       resetStaRetryState();
                       beginStaJoin(millis(), "api-wifi");
                       logWifiStatus("sta-rejoin");
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
