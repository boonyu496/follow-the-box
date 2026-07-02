#include "web/h5_web_server.h"

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

#include <cstdio>

#include "config/network_config.h"
#include "telemetry/debug_console.h"
#include "web/h5_control_routes.h"
#include "web/h5_http_common.h"
#include "web/local_ota_routes.h"
#include "web/telemetry_api.h"
#include "web/wifi_ap_supervisor.h"

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

}  // namespace

void H5WebServer::begin(ProfileStore* profile_store, CalibrationStore* calibration_store,
                        WifiStore* wifi_store, CloudOtaManager* ota_manager) {
  beginWifiApSupervisor(wifi_store);

  g_handler.reset();
  g_ws.onEvent(handleWsEvent);
  g_server.addHandler(&g_ws);

  registerLocalOtaRoutes(g_server, ota_manager);
  registerWifiApSupervisorRoutes(g_server, requireLocalApiAuth);

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

  registerH5ControlRoutes(g_server, g_handler, g_mux, profile_store,
                          calibration_store);

  // GET /api/state -> read-only state snapshot for HTTP polling fallback.
  // This mirrors /ws/state and exists only for AP/LAN UI diagnostics when a
  // browser, proxy, or captive portal interferes with WebSocket transport.
  g_server.on("/api/state", HTTP_GET, [](AsyncWebServerRequest* request) {
    // static: AsyncTCP runs every HTTP callback on the single "async_tcp" task
    // (no reentrancy), and request->send() copies the payload into its own
    // String before returning. Keeping this 3 KB scratch off the task stack
    // (16 KB) is what prevents the async_tcp stack overflow / PANIC reboot that
    // manifested as the hotspot dropping when a phone opened 192.168.4.1.
    static char state[sizeof(g_state_buf)];
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
    // static: see /api/state above. These two buffers total ~5.3 KB and were
    // the dominant async_tcp stack consumer; keeping them off the task stack
    // is the primary fix for the stack-overflow PANIC.
    static char logs[2600];
    if (DebugConsole::copyRecentJson(logs, sizeof(logs)) == 0) {
      std::snprintf(logs, sizeof(logs), "[]");
    }
    static char body[2720];
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
  loopWifiApSupervisor(now_ms);
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
