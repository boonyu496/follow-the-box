#include "ota/cloud_ota_manager.h"

#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config/cloud_config.h"
#include "config/ota_config.h"
#include "telemetry/debug_console.h"

namespace followbox {
namespace {

constexpr size_t kManifestBodySize = 1024;

bool hasText(const char* s) { return s != nullptr && s[0] != '\0'; }

const char* findValue(const char* body, size_t length, const char* key) {
  const size_t key_len = std::strlen(key);
  for (size_t i = 0; i + key_len + 2 <= length; ++i) {
    if (body[i] != '"' || std::strncmp(body + i + 1, key, key_len) != 0) continue;
    const size_t after = i + 1 + key_len;
    if (after >= length || body[after] != '"') continue;
    size_t j = after + 1;
    while (j < length && (body[j] == ' ' || body[j] == '\t')) ++j;
    if (j >= length || body[j] != ':') continue;
    ++j;
    while (j < length && (body[j] == ' ' || body[j] == '\t')) ++j;
    return j < length ? body + j : nullptr;
  }
  return nullptr;
}

bool parseBoolField(const char* body, size_t length, const char* key, bool& out) {
  const char* v = findValue(body, length, key);
  if (v == nullptr) return false;
  const size_t remaining = length - static_cast<size_t>(v - body);
  if (remaining >= 4 && std::strncmp(v, "true", 4) == 0) {
    out = true;
    return true;
  }
  if (remaining >= 5 && std::strncmp(v, "false", 5) == 0) {
    out = false;
    return true;
  }
  return false;
}

bool parseIntField(const char* body, size_t length, const char* key, int& out) {
  const char* v = findValue(body, length, key);
  if (v == nullptr || *v < '0' || *v > '9') return false;
  out = std::atoi(v);
  return true;
}

bool parseStringField(const char* body, size_t length, const char* key,
                      char* out, size_t out_size) {
  const char* v = findValue(body, length, key);
  if (v == nullptr || *v != '"' || out_size == 0) return false;
  ++v;
  size_t i = 0;
  while (i + 1 < out_size && static_cast<size_t>(v - body) + i < length &&
         v[i] != '"') {
    out[i] = v[i];
    ++i;
  }
  out[i] = '\0';
  return i > 0;
}

void joinUrl(char* out, size_t out_size, const char* suffix_or_url) {
  if (std::strncmp(suffix_or_url, "http://", 7) == 0 ||
      std::strncmp(suffix_or_url, "https://", 8) == 0) {
    std::snprintf(out, out_size, "%s", suffix_or_url);
    return;
  }
  std::snprintf(out, out_size, "%s%s", cloud_config::API_BASE_URL, suffix_or_url);
}

}  // namespace

void CloudOtaManager::begin(SafetyCallback safety_callback) {
  safety_callback_ = safety_callback;
  last_check_ms_ = 0;
  in_progress_.store(false);
  portENTER_CRITICAL(&mux_);
  status_ = Status{};
  status_.configured = configured();
  std::snprintf(status_.current_version, sizeof(status_.current_version), "%s",
                ota_config::CURRENT_VERSION);
  check_requested_ = false;
  local_install_requested_ = false;
  local_install_version_[0] = '\0';
  last_request_id_[0] = '\0';
  portEXIT_CRITICAL(&mux_);
}

bool CloudOtaManager::configured() const {
  return ota_config::CLOUD_OTA_ENABLED && cloud_config::ENABLED &&
         hasText(cloud_config::API_BASE_URL) && hasText(cloud_config::DEVICE_ID) &&
         hasText(cloud_config::DEVICE_TOKEN);
}

const char* CloudOtaManager::stateName(State state) {
  switch (state) {
    case State::kIdle: return "idle";
    case State::kChecking: return "checking";
    case State::kUpdateAvailable: return "update_available";
    case State::kInstalling: return "installing";
    case State::kRebooting: return "rebooting";
    case State::kFailed: return "failed";
  }
  return "unknown";
}

CloudOtaManager::Status CloudOtaManager::status() const {
  portENTER_CRITICAL(&mux_);
  const Status copy = status_;
  portEXIT_CRITICAL(&mux_);
  return copy;
}

bool CloudOtaManager::requestCheck() {
  if (!configured() || in_progress_.load()) return false;
  if (WiFi.status() != WL_CONNECTED) {
    setStatus(State::kFailed, false, "", "STA WiFi not connected", millis());
    return false;
  }
  portENTER_CRITICAL(&mux_);
  check_requested_ = true;
  portEXIT_CRITICAL(&mux_);
  return true;
}

bool CloudOtaManager::requestInstall(const char* version) {
  if (!configured() || in_progress_.load() || !hasText(version)) return false;
  bool accepted = false;
  portENTER_CRITICAL(&mux_);
  if (status_.update_available &&
      std::strcmp(status_.available_version, version) == 0) {
    std::snprintf(local_install_version_, sizeof(local_install_version_), "%s", version);
    local_install_requested_ = true;
    check_requested_ = true;
    accepted = true;
  }
  portEXIT_CRITICAL(&mux_);
  return accepted;
}

bool CloudOtaManager::beginLocalUpload(const char* label, char* reason,
                                       size_t reason_size) {
  if (in_progress_.load()) {
    std::snprintf(reason, reason_size, "ota busy");
    return false;
  }

  const char* upload_label = hasText(label) ? label : "local-upload";
  setStatus(State::kInstalling, true, upload_label, "local upload", millis());
  setInProgress(true);
  FB_LOGW("cloud_ota: local upload start label=%s", upload_label);

  if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
    std::snprintf(reason, reason_size, "update begin failed err=%u",
                  static_cast<unsigned>(Update.getError()));
    setStatus(State::kFailed, true, upload_label, reason, millis());
    return false;
  }

  std::snprintf(reason, reason_size, "ok");
  return true;
}

bool CloudOtaManager::writeLocalUpload(uint8_t* data, size_t len,
                                       char* reason, size_t reason_size) {
  if (!in_progress_.load()) {
    std::snprintf(reason, reason_size, "ota not started");
    return false;
  }
  if (data == nullptr && len > 0) {
    std::snprintf(reason, reason_size, "empty chunk");
    setStatus(State::kFailed, true, "local-upload", reason, millis());
    return false;
  }
  const size_t written = Update.write(data, len);
  if (written != len) {
    std::snprintf(reason, reason_size, "write failed err=%u",
                  static_cast<unsigned>(Update.getError()));
    setStatus(State::kFailed, true, "local-upload", reason, millis());
    return false;
  }
  std::snprintf(reason, reason_size, "ok");
  return true;
}

bool CloudOtaManager::finishLocalUpload(char* reason, size_t reason_size) {
  if (!in_progress_.load()) {
    std::snprintf(reason, reason_size, "ota not started");
    return false;
  }
  const bool ended = Update.end(true);
  const bool finished = Update.isFinished();
  if (!ended || !finished) {
    std::snprintf(reason, reason_size, "finish failed err=%u",
                  static_cast<unsigned>(Update.getError()));
    setStatus(State::kFailed, true, "local-upload", reason, millis());
    return false;
  }
  std::snprintf(reason, reason_size, "ok");
  setStatus(State::kRebooting, false, "local-upload",
            "local upload complete; rebooting", millis());
  FB_LOGI("cloud_ota: local upload complete, restarting");
  return true;
}

void CloudOtaManager::abortLocalUpload(const char* reason) {
  if (in_progress_.load()) {
    Update.abort();
    setStatus(State::kFailed, true, "local-upload",
              hasText(reason) ? reason : "local upload aborted", millis());
  }
}

void CloudOtaManager::setStatus(State state, bool update_available,
                                const char* version, const char* reason,
                                uint32_t checked_at_ms) {
  portENTER_CRITICAL(&mux_);
  status_.state = state;
  status_.configured = configured();
  status_.update_available = update_available;
  status_.checked_at_ms = checked_at_ms;
  std::snprintf(status_.current_version, sizeof(status_.current_version), "%s",
                ota_config::CURRENT_VERSION);
  std::snprintf(status_.available_version, sizeof(status_.available_version), "%s",
                version != nullptr ? version : "");
  std::snprintf(status_.reason, sizeof(status_.reason), "%s",
                reason != nullptr ? reason : "");
  portEXIT_CRITICAL(&mux_);
}

void CloudOtaManager::setInProgress(bool active) {
  in_progress_.store(active);
  if (safety_callback_ != nullptr) safety_callback_(active);
}

void CloudOtaManager::update(uint32_t now_ms) {
  if (!configured() || in_progress_.load() || WiFi.status() != WL_CONNECTED) return;

  bool requested = false;
  bool local_install = false;
  char local_version[40] = {0};
  portENTER_CRITICAL(&mux_);
  requested = check_requested_;
  check_requested_ = false;
  local_install = local_install_requested_;
  if (local_install) {
    std::snprintf(local_version, sizeof(local_version), "%s", local_install_version_);
    local_install_requested_ = false;
  }
  portEXIT_CRITICAL(&mux_);

  if (!requested && last_check_ms_ != 0 &&
      now_ms - last_check_ms_ < ota_config::CHECK_INTERVAL_MS) return;
  last_check_ms_ = now_ms;
  setStatus(State::kChecking, false, "", "", now_ms);

  Manifest manifest;
  if (!fetchRequest(manifest) || !manifest.valid) {
    setStatus(State::kFailed, false, "", "check failed", now_ms);
    return;
  }

  const bool available = manifest.update_available &&
                         std::strcmp(manifest.version, ota_config::CURRENT_VERSION) != 0;
  setStatus(available ? State::kUpdateAvailable : State::kIdle, available,
            manifest.version, available ? "user confirmation required" : "up to date",
            now_ms);

  const bool cloud_install = manifest.install_requested && hasText(manifest.request_id);
  if (!available || (!cloud_install && !local_install)) return;
  if (local_install && std::strcmp(local_version, manifest.version) != 0) {
    setStatus(State::kFailed, true, manifest.version, "published version changed", now_ms);
    return;
  }
  if (cloud_install && std::strcmp(last_request_id_, manifest.request_id) == 0) return;

  const char* request_id = cloud_install ? manifest.request_id : "local-h5";
  std::snprintf(last_request_id_, sizeof(last_request_id_), "%s", request_id);
  setStatus(State::kInstalling, true, manifest.version, "installing", now_ms);

  char reason[80] = "ok";
  const bool ok = performUpdate(manifest, reason, sizeof(reason));
  reportResult(request_id, manifest.version, ok, reason);
  if (!ok) {
    // Fail closed: motion remains inhibited until a controlled reboot/USB recovery.
    setStatus(State::kFailed, true, manifest.version, reason, millis());
    return;
  }

  setStatus(State::kRebooting, false, manifest.version, "verified; rebooting", millis());
  FB_LOGI("cloud_ota: update complete version=%s, restarting", manifest.version);
  delay(500);
  ESP.restart();
}

bool CloudOtaManager::fetchRequest(Manifest& manifest) {
  char url[360];
  std::snprintf(url, sizeof(url),
                "%s/api/device/%s/firmware/request?token=%s&current=%s",
                cloud_config::API_BASE_URL, cloud_config::DEVICE_ID,
                cloud_config::DEVICE_TOKEN, ota_config::CURRENT_VERSION);

  HTTPClient http;
  http.setTimeout(ota_config::HTTP_TIMEOUT_MS);
  if (!http.begin(url)) return false;
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  const String payload = http.getString();
  http.end();
  if (payload.length() >= kManifestBodySize) return false;

  char body[kManifestBodySize];
  std::snprintf(body, sizeof(body), "%s", payload.c_str());
  const size_t length = std::strlen(body);

  bool ok = false;
  parseBoolField(body, length, "ok", ok);
  parseBoolField(body, length, "update_available", manifest.update_available);
  parseBoolField(body, length, "install_requested", manifest.install_requested);
  parseIntField(body, length, "size", manifest.size);
  const bool has_version = parseStringField(body, length, "available_version",
                                             manifest.version, sizeof(manifest.version));
  const bool has_url = parseStringField(body, length, "url", manifest.url,
                                         sizeof(manifest.url));
  const bool has_md5 = parseStringField(body, length, "md5", manifest.md5,
                                         sizeof(manifest.md5));
  parseStringField(body, length, "request_id", manifest.request_id,
                   sizeof(manifest.request_id));
  manifest.valid = ok && has_version && has_url && has_md5 &&
                   std::strlen(manifest.md5) == 32 && manifest.size > 0;
  return manifest.valid;
}

bool CloudOtaManager::performUpdate(const Manifest& manifest, char* reason,
                                    size_t reason_size) {
  char url[360];
  joinUrl(url, sizeof(url), manifest.url);
  setInProgress(true);
  FB_LOGW("cloud_ota: authorized start version=%s", manifest.version);

  HTTPClient http;
  http.setTimeout(ota_config::HTTP_TIMEOUT_MS);
  if (!http.begin(url)) {
    std::snprintf(reason, reason_size, "bad download url");
    return false;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    std::snprintf(reason, reason_size, "download http %d", code);
    http.end();
    return false;
  }

  const int content_length = http.getSize();
  if (content_length != manifest.size) {
    std::snprintf(reason, reason_size, "size mismatch");
    http.end();
    return false;
  }
  if (!Update.begin(static_cast<size_t>(manifest.size), U_FLASH)) {
    std::snprintf(reason, reason_size, "update begin failed");
    http.end();
    return false;
  }
  if (!Update.setMD5(manifest.md5)) {
    std::snprintf(reason, reason_size, "invalid md5");
    Update.abort();
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  const size_t written = Update.writeStream(*stream);
  const bool ended = Update.end();
  const bool finished = Update.isFinished();
  http.end();
  if (!ended || !finished || written != static_cast<size_t>(manifest.size)) {
    std::snprintf(reason, reason_size, "write failed err=%u",
                  static_cast<unsigned>(Update.getError()));
    Update.abort();
    return false;
  }
  std::snprintf(reason, reason_size, "ok");
  return true;
}

void CloudOtaManager::reportResult(const char* request_id, const char* version,
                                   bool ok, const char* reason) {
  char url[300];
  std::snprintf(url, sizeof(url), "%s/api/device/%s/ota-result",
                cloud_config::API_BASE_URL, cloud_config::DEVICE_ID);
  char payload[320];
  std::snprintf(payload, sizeof(payload),
                "{\"token\":\"%s\",\"request_id\":\"%s\",\"version\":\"%s\","
                "\"ok\":%s,\"reason\":\"%s\"}",
                cloud_config::DEVICE_TOKEN, request_id != nullptr ? request_id : "",
                version, ok ? "true" : "false", reason != nullptr ? reason : "");
  HTTPClient http;
  http.setTimeout(cloud_config::HTTP_TIMEOUT_MS);
  if (http.begin(url)) {
    http.addHeader("Content-Type", "application/json");
    http.POST(reinterpret_cast<uint8_t*>(payload), std::strlen(payload));
  }
  http.end();
}

}  // namespace followbox
