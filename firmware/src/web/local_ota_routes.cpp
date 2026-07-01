#include "web/local_ota_routes.h"

#include <Arduino.h>

#include <cctype>
#include <cstdio>
#include <cstring>

#include "web/h5_http_common.h"

namespace followbox {
namespace {

bool g_local_ota_upload_started = false;
bool g_local_ota_upload_ok = false;
int g_local_ota_upload_status = 400;
char g_local_ota_upload_reason[96] = {0};

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

}  // namespace

void registerLocalOtaRoutes(AsyncWebServer& server, CloudOtaManager* ota_manager) {
  server.on("/ota-upload", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html; charset=utf-8", localOtaUploadPage());
  });

  server.on("/api/ota/status", HTTP_GET,
            [ota_manager](AsyncWebServerRequest* request) {
    if (ota_manager == nullptr) {
      request->send(503, "application/json",
                    "{\"ok\":false,\"reason\":\"ota unavailable\"}");
      return;
    }
    const CloudOtaManager::Status status = ota_manager->status();
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

  server.on("/api/ota/check", HTTP_POST,
            [ota_manager](AsyncWebServerRequest* request) {
    if (!requireLocalApiAuth(request)) return;
    const bool accepted = ota_manager != nullptr && ota_manager->requestCheck();
    request->send(accepted ? 202 : 409, "application/json",
                  accepted ? kAckOk :
                  "{\"ok\":false,\"reason\":\"ota unavailable or busy\"}");
  });

  server.on(
      "/api/ota/install", HTTP_POST, [](AsyncWebServerRequest* request) {}, nullptr,
      [ota_manager](AsyncWebServerRequest* request, uint8_t* data, size_t len,
                    size_t index, size_t total) {
        onH5Body(request, data, len, index, total,
                 [ota_manager](AsyncWebServerRequest* req, const char* body,
                               size_t length) {
                   if (!requireLocalApiAuth(req)) return;
                   char version[40];
                   const bool parsed = parseVersion(body, length, version,
                                                    sizeof(version));
                   const bool accepted = parsed && ota_manager != nullptr &&
                                         ota_manager->requestInstall(version);
                   req->send(accepted ? 202 : 409, "application/json",
                             accepted ? kAckOk :
                             "{\"ok\":false,\"reason\":\"version not checked or changed\"}");
                 });
      });

  server.on(
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
      [ota_manager](AsyncWebServerRequest* request, const String& filename,
                    size_t index, uint8_t* data, size_t len, bool final) {
        if (index == 0) {
          g_local_ota_upload_started = false;
          setLocalOtaUploadResult(400, "");
          if (!localApiAuthorized(request)) {
            setLocalOtaUploadResult(401, "unauthorized");
            return;
          }
          if (ota_manager == nullptr) {
            setLocalOtaUploadResult(503, "ota unavailable");
            return;
          }
          if (!filename.endsWith(".bin")) {
            setLocalOtaUploadResult(400, "firmware must be .bin");
            return;
          }
          char reason[80] = {0};
          if (!ota_manager->beginLocalUpload("local-upload", reason,
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
          if (!ota_manager->writeLocalUpload(data, len, reason,
                                             sizeof(reason))) {
            setLocalOtaUploadResult(500, reason);
            return;
          }
        }
        if (final) {
          char reason[80] = {0};
          if (ota_manager->finishLocalUpload(reason, sizeof(reason))) {
            setLocalOtaUploadResult(200, "ok");
          } else {
            setLocalOtaUploadResult(500, reason);
          }
        }
      });
}

}  // namespace followbox
