#include "web/h5_web_server.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WiFi.h>

#include <cstdio>
#include <cctype>
#include <cstring>

#include "config/network_config.h"
#include "telemetry/debug_console.h"
#include "web/h5_request_parser.h"
#include "web/telemetry_api.h"

namespace followbox {
namespace {

// Transport singletons (single H5WebServer instance). Kept in the .cpp so the
// header stays free of Arduino/AsyncWebServer includes.
AsyncWebServer g_server(net::HTTP_PORT);
AsyncWebSocket g_ws("/ws/state");
H5CommandHandler g_handler;

// Guards g_handler: touched by AsyncTCP-task HTTP/WS callbacks and the loop
// task. Critical sections stay tiny (one handler call / struct copy).
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
 
uint32_t g_last_push_ms = 0;
char g_state_buf[3072];
bool g_state_valid = false;
portMUX_TYPE g_state_mux = portMUX_INITIALIZER_UNLOCKED;
 
ProfileStore* g_profile_store = nullptr;
CalibrationStore* g_calibration_store = nullptr;
WifiStore* g_wifi_store = nullptr;
CloudOtaManager* g_ota_manager = nullptr;
// Cached provisioned SSID so /api/wifi/status never re-reads NVS per poll.
char g_sta_ssid[33] = {0};
bool g_local_ota_upload_started = false;
bool g_local_ota_upload_ok = false;
int g_local_ota_upload_status = 400;
char g_local_ota_upload_reason[96] = {0};

// Small fixed JSON replies (no heap).
constexpr char kAckOk[] = "{\"ok\":true}";
constexpr char kAckRejected[] = "{\"ok\":false}";
constexpr char kAckUnauthorized[] = "{\"ok\":false,\"reason\":\"unauthorized\"}";

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

void logWifiStatus(const char* reason) {
  char ap_ip[16];
  char sta_ip[16];
  formatIp(WiFi.softAPIP(), ap_ip, sizeof(ap_ip));
  formatIp(WiFi.localIP(), sta_ip, sizeof(sta_ip));
  const wifi_mode_t mode = WiFi.getMode();
  const int sta_status = static_cast<int>(WiFi.status());
  const bool ap_ready = softApReady();
  const int rssi = sta_status == WL_CONNECTED ? static_cast<int>(WiFi.RSSI()) : 0;
  FB_LOGI("wifi: %s mode=%d channel=%d ap=%d ap_ip=%s ap_clients=%u sta=%d sta_ip=%s rssi=%d ssid=%s",
          reason != nullptr ? reason : "status", static_cast<int>(mode),
          static_cast<int>(WiFi.channel()), ap_ready ? 1 : 0, ap_ip,
          static_cast<unsigned>(WiFi.softAPgetStationNum()), sta_status, sta_ip,
          rssi, g_sta_ssid[0] != '\0' ? g_sta_ssid : "-");
}

bool startSoftAp(const char* reason) {
  if (!wifiModeHasAp(WiFi.getMode())) {
    WiFi.mode(WIFI_AP_STA);
  }
  const bool ok = WiFi.softAP(net::SOFT_AP_SSID, net::SOFT_AP_PASSWORD,
                              net::SOFT_AP_CHANNEL, /*ssid_hidden=*/0,
                              net::SOFT_AP_MAX_CONN);
  FB_LOGI("wifi_ap: start reason=%s ok=%d ssid=%s channel=%u max_conn=%u",
          reason != nullptr ? reason : "unknown", ok ? 1 : 0,
          net::SOFT_AP_SSID, static_cast<unsigned>(net::SOFT_AP_CHANNEL),
          static_cast<unsigned>(net::SOFT_AP_MAX_CONN));
  logWifiStatus(reason);
  return ok;
}

bool constantTimeEquals(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) {
    return false;
  }
  const size_t a_len = std::strlen(a);
  const size_t b_len = std::strlen(b);
  uint8_t diff = static_cast<uint8_t>(a_len ^ b_len);
  const size_t max_len = a_len > b_len ? a_len : b_len;
  for (size_t i = 0; i < max_len; ++i) {
    const uint8_t ca = i < a_len ? static_cast<uint8_t>(a[i]) : 0;
    const uint8_t cb = i < b_len ? static_cast<uint8_t>(b[i]) : 0;
    diff |= static_cast<uint8_t>(ca ^ cb);
  }
  return diff == 0;
}

bool localApiAuthorized(AsyncWebServerRequest* request) {
  if (!net::LOCAL_API_AUTH_REQUIRED) {
    return true;
  }
  if (net::LOCAL_API_KEY[0] == '\0') {
    return false;
  }
  const String header = request->header(net::LOCAL_API_KEY_HEADER);
  if (constantTimeEquals(header.c_str(), net::LOCAL_API_KEY)) {
    return true;
  }
  if (request->hasParam("key")) {
    return constantTimeEquals(request->getParam("key")->value().c_str(),
                              net::LOCAL_API_KEY);
  }
  return false;
}

bool requireLocalApiAuth(AsyncWebServerRequest* request) {
  if (localApiAuthorized(request)) {
    return true;
  }
  request->send(401, "application/json", kAckUnauthorized);
  return false;
}

const char* localOtaUploadPage() {
  return R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>FollowBox OTA</title>
  <style>
    body{font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;margin:0;background:#f6f7f9;color:#18212f}
    main{max-width:520px;margin:0 auto;padding:28px 18px}
    section{background:#fff;border:1px solid #d9dee8;border-radius:8px;padding:18px;box-shadow:0 8px 24px rgba(18,33,47,.06)}
    h1{font-size:22px;margin:0 0 12px}
    label{display:block;margin:14px 0 6px;font-weight:600}
    input,button,progress{width:100%;box-sizing:border-box}
    input{padding:11px;border:1px solid #c9d0dc;border-radius:6px;background:#fff}
    button{margin-top:16px;padding:12px;border:0;border-radius:6px;background:#2563eb;color:#fff;font-weight:700}
    button:disabled{opacity:.55}
    progress{margin-top:14px;height:18px}
    p{line-height:1.5;color:#4b5563}
    #state{min-height:24px;font-weight:700;color:#166534}
  </style>
</head>
<body>
  <main>
    <section>
      <h1>FollowBox 本地 OTA</h1>
      <p>选择 PlatformIO 编译生成的 firmware.bin，上传完成后设备会自动重启。</p>
      <label for="file">固件文件</label>
      <input id="file" type="file" accept=".bin,application/octet-stream">
      <label for="key">本地控制 Key</label>
      <input id="key" type="password" placeholder="未启用授权时可留空">
      <button id="upload">上传并重启</button>
      <progress id="progress" value="0" max="100"></progress>
      <p id="state">等待选择文件</p>
    </section>
  </main>
  <script>
    const file = document.getElementById("file");
    const key = document.getElementById("key");
    const btn = document.getElementById("upload");
    const bar = document.getElementById("progress");
    const state = document.getElementById("state");
    btn.addEventListener("click", () => {
      if (!file.files.length) { state.textContent = "请先选择 firmware.bin"; return; }
      if (!confirm("开始本地 OTA？上传期间请勿断电，车辆会强制停止并自动重启。")) return;
      const data = new FormData();
      data.append("firmware", file.files[0], file.files[0].name);
      const xhr = new XMLHttpRequest();
      xhr.open("POST", "/api/ota/local-upload");
      if (key.value) xhr.setRequestHeader("X-FollowBox-Key", key.value);
      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) bar.value = Math.round((e.loaded / e.total) * 100);
      };
      xhr.onload = () => {
        btn.disabled = false;
        state.textContent = xhr.status >= 200 && xhr.status < 300
          ? "上传完成，设备正在重启"
          : "上传失败：" + (xhr.responseText || xhr.status);
      };
      xhr.onerror = () => { btn.disabled = false; state.textContent = "网络中断或设备已重启"; };
      btn.disabled = true;
      state.textContent = "上传中";
      xhr.send(data);
    });
  </script>
</body>
</html>)HTML";
}

void scheduleLocalOtaRestart() {
  xTaskCreate(
      [](void* /*arg*/) {
        vTaskDelay(pdMS_TO_TICKS(800));
        ESP.restart();
        vTaskDelete(nullptr);
      },
      "local_ota_reboot", 2048, nullptr, 1, nullptr);
}

void setLocalOtaUploadResult(int status, const char* reason) {
  g_local_ota_upload_status = status;
  g_local_ota_upload_ok = status >= 200 && status < 300;
  std::snprintf(g_local_ota_upload_reason, sizeof(g_local_ota_upload_reason), "%s",
                reason != nullptr ? reason : "");
}

bool parseVersion(const char* body, size_t length, char* out, size_t out_size) {
  constexpr char kKey[] = "\"version\"";
  if (body == nullptr || out == nullptr || out_size == 0) return false;
  const char* key = std::strstr(body, kKey);
  if (key == nullptr || static_cast<size_t>(key - body) >= length) return false;
  const char* colon = std::strchr(key + sizeof(kKey) - 1, ':');
  if (colon == nullptr || static_cast<size_t>(colon - body) >= length) return false;
  const char* value = colon + 1;
  while (static_cast<size_t>(value - body) < length &&
         (*value == ' ' || *value == '\t')) ++value;
  if (static_cast<size_t>(value - body) >= length || *value != '"') return false;
  ++value;
  size_t i = 0;
  while (static_cast<size_t>(value - body) + i < length && value[i] != '"' &&
         i + 1 < out_size) {
    const char c = value[i];
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' ||
          c == '_')) return false;
    out[i++] = c;
  }
  out[i] = '\0';
  return i > 0 && static_cast<size_t>(value - body) + i < length && value[i] == '"';
}

void handleWsEvent(AsyncWebSocket* /*server*/, AsyncWebSocketClient* /*client*/,
                   AwsEventType type, void* /*arg*/, uint8_t* /*data*/,
                   size_t /*len*/) {
  const uint32_t now = millis();
  if (type == WS_EVT_CONNECT) {
    portENTER_CRITICAL(&g_mux);
    g_handler.onConnect(now);
    portEXIT_CRITICAL(&g_mux);
  } else if (type == WS_EVT_DISCONNECT || type == WS_EVT_ERROR) {
    portENTER_CRITICAL(&g_mux);
    g_handler.onDisconnect();
    portEXIT_CRITICAL(&g_mux);
  }
  // Inbound WS frames are ignored: control comes via the POST endpoints so the
  // panel cannot stream raw motion through the socket.
}

// Accumulate a (small) POST body into a stack buffer, invoking fn once the full
// body has arrived. Oversized bodies are rejected (fail safe) rather than
// truncated into a partial command.
template <typename Fn>
void onBody(AsyncWebServerRequest* request, uint8_t* data, size_t len,
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

}  // namespace

void H5WebServer::begin(ProfileStore* profile_store, CalibrationStore* calibration_store,
                        WifiStore* wifi_store, CloudOtaManager* ota_manager) {
  g_profile_store = profile_store;
  g_calibration_store = calibration_store;
  g_wifi_store = wifi_store;
  g_ota_manager = ota_manager;

  // AP + STA simultaneously: the AP is the always-available provisioning and
  // local-control channel (the box can never become unreachable after a bad
  // OTA/config), while the STA leg joins the user's WiFi for the cloud link
  // once credentials have been provisioned via POST /api/wifi.
  WiFi.persistent(false);  // creds live in our own NVS namespace, not the SDK's
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);  // keep AP beacons steady while STA/cloud is active.
  startSoftAp("boot");
  WiFi.setAutoReconnect(true);

  WifiCredentials creds;
  if (g_wifi_store != nullptr) {
    creds = g_wifi_store->load();
  }
  if (creds.valid()) {
    std::snprintf(g_sta_ssid, sizeof(g_sta_ssid), "%s", creds.ssid);
    WiFi.begin(creds.ssid, creds.password);
  } else if (net::STA_SSID[0] != '\0') {
    // Compile-time fallback for bench builds (FOLLOWBOX_WIFI_STA legacy path).
    if (g_wifi_store != nullptr) {
      WifiCredentials fallback;
      std::snprintf(fallback.ssid, sizeof(fallback.ssid), "%s", net::STA_SSID);
      std::snprintf(fallback.password, sizeof(fallback.password), "%s",
                    net::STA_PASSWORD);
      g_wifi_store->save(fallback);
    }
    std::snprintf(g_sta_ssid, sizeof(g_sta_ssid), "%s", net::STA_SSID);
    WiFi.begin(net::STA_SSID, net::STA_PASSWORD);
  }

  g_handler.reset();
  g_ws.onEvent(handleWsEvent);
  g_server.addHandler(&g_ws);

  // Built into the app firmware so the bench OTA page remains reachable even
  // when LittleFS assets are stale or missing. Register before serveStatic("/")
  // so the catch-all static handler cannot claim this path.
  g_server.on("/ota-upload", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html; charset=utf-8", localOtaUploadPage());
  });

  // Static H5 panel from LittleFS (firmware/web/, flashed via `pio run -t
  // uploadfs`). Mount failure is non-fatal: the API/WS still work, only the
  // bundled UI is unavailable. NOTE: the partition in ota_8MB.csv is *named*
  // "littlefs", and LittleFS.begin() defaults to the label "spiffs", so the
  // label must be passed explicitly or the mount silently fails.
  if (LittleFS.begin(/*formatOnFail=*/false, "/littlefs", 10, "littlefs")) {
    auto& static_files = g_server.serveStatic("/", LittleFS, "/");
    static_files.setDefaultFile("index.html");
    static_files.setCacheControl("no-store, no-cache, must-revalidate, max-age=0");
  }

  // POST /api/jog -> low-speed jog (deadman gated, replay protected downstream).
  g_server.on(
      "/api/jog", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
      [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index,
         size_t total) {
        onBody(request, data, len, index, total,
               [](AsyncWebServerRequest* req, const char* body, size_t length) {
                 if (!requireLocalApiAuth(req)) {
                   return;
                 }
                 JogRequest jog = parseJogRequest(body, length);
                 if (!jog.valid) {
                   req->send(400, "application/json", kAckRejected);
                   return;
                 }
                 const uint32_t now = millis();
                 portENTER_CRITICAL(&g_mux);
                 const bool applied = g_handler.onJog(jog.seq, jog.forward,
                                                       jog.turn, jog.deadman, now);
                 portEXIT_CRITICAL(&g_mux);
                 req->send(200, "application/json",
                           applied ? kAckOk : kAckRejected);
               });
      });

  // POST /api/mode-request -> mode request (mode_manager still validates).
  g_server.on(
      "/api/mode-request", HTTP_POST,
      [](AsyncWebServerRequest* request) {}, nullptr,
      [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index,
         size_t total) {
        onBody(request, data, len, index, total,
               [](AsyncWebServerRequest* req, const char* body, size_t length) {
                 if (!requireLocalApiAuth(req)) {
                   return;
                 }
                 H5ModeRequest mode = parseModeRequest(body, length);
                 const uint32_t now = millis();
                 portENTER_CRITICAL(&g_mux);
                 g_handler.onModeRequest(mode, now);
                 portEXIT_CRITICAL(&g_mux);
                 req->send(200, "application/json", kAckOk);
               });
      });

  // POST /api/reset-fault -> software fault-reset request.
  g_server.on(
      "/api/reset-fault", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
      [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index,
         size_t total) {
        onBody(request, data, len, index, total,
               [](AsyncWebServerRequest* req, const char* body, size_t length) {
                 if (!requireLocalApiAuth(req)) {
                   return;
                 }
                 const bool confirm = parseResetFaultRequest(body, length);
                 const uint32_t now = millis();
                 portENTER_CRITICAL(&g_mux);
                 const bool applied = g_handler.onResetFault(confirm, now);
                 portEXIT_CRITICAL(&g_mux);
                 req->send(200, "application/json",
                           applied ? kAckOk : kAckRejected);
               });
      });

  // POST /api/calibrate -> save throttle calibration to NVS and update control loop
  g_server.on(
      "/api/calibrate", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
      [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index,
         size_t total) {
        onBody(request, data, len, index, total,
               [](AsyncWebServerRequest* req, const char* body, size_t length) {
                 if (!requireLocalApiAuth(req)) {
                   return;
                 }
                 CalibrateRequest cal_req = parseCalibrateRequest(body, length);
                 if (!cal_req.valid || g_calibration_store == nullptr) {
                   req->send(400, "application/json", kAckRejected);
                   return;
                 }
                 ThrottleCalibration cal;
                 cal.deadband_mv = static_cast<int>(cal_req.deadband_mv);
                 cal.min_active_mv = static_cast<int>(cal_req.min_active_mv);
                 cal.max_mv = static_cast<int>(cal_req.max_mv);
                 cal.module_full_scale_mv = static_cast<int>(cal_req.module_full_scale_mv);
                 cal.rise_mv_per_s = static_cast<int>(cal_req.rise_mv_per_s);
                 cal.fall_mv_per_s = static_cast<int>(cal_req.fall_mv_per_s);

                 // Write to NVS asynchronously in Core 0 (AsyncTCP thread)
                 const bool saved = g_calibration_store->save(cal);

                 const uint32_t now = millis();
                 portENTER_CRITICAL(&g_mux);
                 const bool applied = g_handler.onCalibrate(cal, now);
                 portEXIT_CRITICAL(&g_mux);

                 req->send(200, "application/json",
                           (saved && applied) ? kAckOk : kAckRejected);
               });
      });

  // POST /api/wizard-complete -> mark install wizard as complete in NVS and update control loop
  g_server.on(
      "/api/wizard-complete", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
      [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index,
         size_t total) {
        onBody(request, data, len, index, total,
               [](AsyncWebServerRequest* req, const char* body, size_t length) {
                 if (!requireLocalApiAuth(req)) {
                   return;
                 }
                 WizardRequest wiz_req = parseWizardRequest(body, length);
                 if (!wiz_req.valid || g_profile_store == nullptr) {
                   req->send(400, "application/json", kAckRejected);
                   return;
                 }
                 RuntimeProfile prof;
                 prof.install_wizard_complete = wiz_req.complete;

                 // Write to NVS asynchronously in Core 0
                 const bool saved = g_profile_store->save(prof);

                 const uint32_t now = millis();
                 portENTER_CRITICAL(&g_mux);
                 const bool applied = g_handler.onWizardComplete(wiz_req.complete, now);
                 portEXIT_CRITICAL(&g_mux);

                 req->send(200, "application/json",
                           (saved && applied) ? kAckOk : kAckRejected);
               });
      });

  // POST /api/wifi -> persist STA credentials and (re)join. Pure transport:
  // touches WiFi/NVS only, never the motion path. The AP stays up regardless,
  // so a wrong password can always be corrected from the hotspot.
  g_server.on(
      "/api/wifi", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
      [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index,
         size_t total) {
        onBody(request, data, len, index, total,
               [](AsyncWebServerRequest* req, const char* body, size_t length) {
                 if (!requireLocalApiAuth(req)) {
                   return;
                 }
                 const WifiConfigRequest wifi = parseWifiConfigRequest(body, length);
                 if (!wifi.valid || g_wifi_store == nullptr) {
                   req->send(400, "application/json", kAckRejected);
                   return;
                 }
                 WifiCredentials creds;
                 std::snprintf(creds.ssid, sizeof(creds.ssid), "%s", wifi.ssid);
                 std::snprintf(creds.password, sizeof(creds.password), "%s",
                               wifi.password);
                 const bool saved = g_wifi_store->save(creds);
                 // Reply before rejoining so the panel (often on the AP leg)
                 // gets its ack even if the STA leg churns the radio.
                 req->send(200, "application/json",
                           saved ? kAckOk : kAckRejected);
                 if (saved) {
                   std::snprintf(g_sta_ssid, sizeof(g_sta_ssid), "%s",
                                  creds.ssid);
                   WiFi.disconnect(/*wifioff=*/false, /*eraseap=*/false);
                   WiFi.begin(creds.ssid, creds.password);
                   logWifiStatus("sta-rejoin");
                 }
               });
      });

  // GET /api/wifi/status -> STA join state for the provisioning UI.
  g_server.on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    char sta_ip[16];
    char ap_ip[16];
    formatIp(WiFi.localIP(), sta_ip, sizeof(sta_ip));
    formatIp(WiFi.softAPIP(), ap_ip, sizeof(ap_ip));
    const bool connected = WiFi.status() == WL_CONNECTED;
    const bool ap_ready = softApReady();
    char buf[384];
    std::snprintf(buf, sizeof(buf),
                  "{\"ok\":true,\"provisioned\":%s,\"sta_connected\":%s,"
                  "\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,"
                  "\"ap_ready\":%s,\"ap_ssid\":\"%s\",\"ap_ip\":\"%s\","
                  "\"ap_clients\":%u,\"wifi_mode\":%d,\"wifi_channel\":%d}",
                  g_sta_ssid[0] != '\0' ? "true" : "false",
                  connected ? "true" : "false", g_sta_ssid,
                  connected ? sta_ip : "",
                  connected ? static_cast<int>(WiFi.RSSI()) : 0,
                  ap_ready ? "true" : "false", net::SOFT_AP_SSID, ap_ip,
                  static_cast<unsigned>(WiFi.softAPgetStationNum()),
                  static_cast<int>(WiFi.getMode()), static_cast<int>(WiFi.channel()));
    request->send(200, "application/json", buf);
  });

  // OTA is explicit-consent only. Checking never writes flash; installation
  // accepts only the exact version returned by the last successful check.
  g_server.on("/api/ota/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (g_ota_manager == nullptr) {
      request->send(503, "application/json",
                    "{\"ok\":false,\"reason\":\"ota unavailable\"}");
      return;
    }
    const CloudOtaManager::Status status = g_ota_manager->status();
    char body[384];
    std::snprintf(body, sizeof(body),
                  "{\"ok\":true,\"configured\":%s,\"state\":\"%s\","
                  "\"current_version\":\"%s\",\"available_version\":\"%s\","
                  "\"update_available\":%s,\"checked_at_ms\":%u,\"reason\":\"%s\"}",
                  status.configured ? "true" : "false",
                  CloudOtaManager::stateName(status.state), status.current_version,
                  status.available_version, status.update_available ? "true" : "false",
                  static_cast<unsigned>(status.checked_at_ms), status.reason);
    request->send(200, "application/json", body);
  });

  g_server.on("/api/ota/check", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!requireLocalApiAuth(request)) return;
    const bool accepted = g_ota_manager != nullptr && g_ota_manager->requestCheck();
    request->send(accepted ? 202 : 409, "application/json",
                  accepted ? kAckOk :
                  "{\"ok\":false,\"reason\":\"ota unavailable or busy\"}");
  });

  g_server.on(
      "/api/ota/install", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
      [](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index,
         size_t total) {
        onBody(request, data, len, index, total,
               [](AsyncWebServerRequest* req, const char* body, size_t length) {
                 if (!requireLocalApiAuth(req)) return;
                 char version[40];
                 const bool parsed = parseVersion(body, length, version, sizeof(version));
                 const bool accepted = parsed && g_ota_manager != nullptr &&
                                       g_ota_manager->requestInstall(version);
                 req->send(accepted ? 202 : 409, "application/json",
                           accepted ? kAckOk :
                           "{\"ok\":false,\"reason\":\"version not checked or changed\"}");
               });
      });

  // Local OTA upload endpoint for bench testing. This updates the app firmware
  // only; LittleFS assets still use uploadfs when needed.
  g_server.on(
      "/api/ota/local-upload", HTTP_POST,
      [](AsyncWebServerRequest* request) {
        if (!g_local_ota_upload_started && g_local_ota_upload_reason[0] == '\0') {
          setLocalOtaUploadResult(400, "no firmware file");
        }
        char body[192];
        std::snprintf(body, sizeof(body),
                      "{\"ok\":%s,\"rebooting\":%s,\"reason\":\"%s\"}",
                      g_local_ota_upload_ok ? "true" : "false",
                      g_local_ota_upload_ok ? "true" : "false",
                      g_local_ota_upload_reason);
        request->send(g_local_ota_upload_status, "application/json", body);
        if (g_local_ota_upload_ok) {
          scheduleLocalOtaRestart();
        }
      },
      [](AsyncWebServerRequest* request, const String& filename, size_t index,
         uint8_t* data, size_t len, bool final) {
        if (index == 0) {
          g_local_ota_upload_started = false;
          setLocalOtaUploadResult(400, "");
          if (!localApiAuthorized(request)) {
            setLocalOtaUploadResult(401, "unauthorized");
            return;
          }
          if (g_ota_manager == nullptr) {
            setLocalOtaUploadResult(503, "ota unavailable");
            return;
          }
          if (!filename.endsWith(".bin")) {
            setLocalOtaUploadResult(400, "firmware must be .bin");
            return;
          }
          char reason[80] = {0};
          if (!g_ota_manager->beginLocalUpload("local-upload", reason,
                                               sizeof(reason))) {
            setLocalOtaUploadResult(409, reason);
            return;
          }
          g_local_ota_upload_started = true;
          setLocalOtaUploadResult(202, "uploading");
        }

        if (!g_local_ota_upload_started || g_local_ota_upload_status >= 400) {
          return;
        }
        if (len > 0) {
          char reason[80] = {0};
          if (!g_ota_manager->writeLocalUpload(data, len, reason,
                                               sizeof(reason))) {
            setLocalOtaUploadResult(500, reason);
            return;
          }
        }
        if (final) {
          char reason[80] = {0};
          if (g_ota_manager->finishLocalUpload(reason, sizeof(reason))) {
            setLocalOtaUploadResult(200, "ok");
          } else {
            setLocalOtaUploadResult(500, reason);
          }
        }
      });

  // GET /api/state -> read-only state snapshot for HTTP polling fallback.
  // This mirrors /ws/state and exists only for AP/LAN UI diagnostics when a
  // browser, proxy, or captive portal interferes with WebSocket transport.
  g_server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* request) {
    char state[sizeof(g_state_buf)];
    bool valid = false;
    portENTER_CRITICAL(&g_state_mux);
    valid = g_state_valid;
    if (valid) {
      std::snprintf(state, sizeof(state), "%s", g_state_buf);
    }
    portEXIT_CRITICAL(&g_state_mux);

    if (!valid) {
      request->send(503, "application/json",
                    "{\"ok\":false,\"reason\":\"state not ready\"}");
      return;
    }
    request->send(200, "application/json", state);
  });

  // GET /api/local-auth/status -> read-only diagnostics for the H5 Key panel.
  // It never exposes the secret; it only tells the operator whether this build
  // requires X-FollowBox-Key for local write endpoints.
  g_server.on("/api/local-auth/status", HTTP_GET,
              [](AsyncWebServerRequest* request) {
                char body[160];
                std::snprintf(body, sizeof(body),
                              "{\"ok\":true,\"auth_required\":%s,"
                              "\"key_configured\":%s,\"key_header\":\"%s\"}",
                              net::LOCAL_API_AUTH_REQUIRED ? "true" : "false",
                              net::LOCAL_API_KEY[0] != '\0' ? "true" : "false",
                              net::LOCAL_API_KEY_HEADER);
                request->send(200, "application/json", body);
              });

  // GET /api/logs -> read-only local diagnostics. It does not drain the ring,
  // so cloud telemetry can still upload the same recent lines.
  g_server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest* request) {
    char logs[2600];
    if (DebugConsole::copyRecentJson(logs, sizeof(logs)) == 0) {
      std::snprintf(logs, sizeof(logs), "[]");
    }
    char body[2720];
    const int written = std::snprintf(body, sizeof(body),
                                      "{\"ok\":true,\"logs\":%s}", logs);
    if (written < 0 || static_cast<size_t>(written) >= sizeof(body)) {
      request->send(500, "application/json", "{\"ok\":false}");
      return;
    }
    request->send(200, "application/json", body);
  });

  g_server.onNotFound([](AsyncWebServerRequest* request) {
    request->send(404, "application/json", "{\"ok\":false,\"reason\":\"not found\"}");
  });

  g_server.begin();
}

H5ControlInput H5WebServer::pollInput(uint32_t now_ms, App& app, DriveAdapterAnalogBldc& drive) {
  H5ControlInput snapshot;
  portENTER_CRITICAL(&g_mux);
  g_handler.update(now_ms);
  snapshot = g_handler.input();

  // Defer calibrate/wizard to the Runtime control loop; the transport layer
  // must not call drive/app directly (CODE-REVIEW-H5-2026-06-15 P0-1).
  if (g_handler.hasPendingCalibrate()) {
    const auto& cal = g_handler.getPendingCalibrate();
    snapshot.pending_calibrate = true;
    snapshot.cal_deadband_mv = cal.deadband_mv;
    snapshot.cal_min_active_mv = cal.min_active_mv;
    snapshot.cal_max_mv = cal.max_mv;
    snapshot.cal_module_full_scale_mv = cal.module_full_scale_mv;
    snapshot.cal_rise_mv_per_s = cal.rise_mv_per_s;
    snapshot.cal_fall_mv_per_s = cal.fall_mv_per_s;
    g_handler.clearPendingCalibrate();
  }
  if (g_handler.hasPendingWizard()) {
    snapshot.pending_wizard_complete = g_handler.getPendingWizard();
    g_handler.clearPendingWizard();
  }

  // Clear one-shot requests in the handler
  g_handler.clearOneShotRequests();

  portEXIT_CRITICAL(&g_mux);
  return snapshot;
}

void H5WebServer::pushState(const SystemState& state, uint32_t now_ms) {
  g_ws.cleanupClients();
  if (now_ms - g_last_push_ms < net::STATE_PUSH_INTERVAL_MS) {
    return;
  }
  g_last_push_ms = now_ms;
  char state_buf[sizeof(g_state_buf)];
  const size_t written = buildStateJson(state, state_buf, sizeof(state_buf));
  if (written > 0) {
    portENTER_CRITICAL(&g_state_mux);
    std::snprintf(g_state_buf, sizeof(g_state_buf), "%s", state_buf);
    g_state_valid = true;
    portEXIT_CRITICAL(&g_state_mux);
  }
  if (written > 0 && g_ws.count() > 0) {
    g_ws.textAll(state_buf, written);
  }
}

}  // namespace followbox
