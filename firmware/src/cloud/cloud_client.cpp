#include "cloud/cloud_client.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>

#include "config/cloud_config.h"
#include "config/profile_defaults.h"
#include "core/math_utils.h"
#include "core/time_utils.h"
#include "telemetry/debug_console.h"
#include "web/telemetry_api.h"

namespace followbox {
namespace {

constexpr size_t kStateJsonSize = 3072;
constexpr size_t kLogsJsonSize = 2600;
constexpr size_t kPayloadSize = 6400;
constexpr size_t kCommandBodySize = 384;

bool hasText(const char* s) {
  return s != nullptr && s[0] != '\0';
}

const char* findValue(const char* body, size_t length, const char* key) {
  const size_t key_len = std::strlen(key);
  if (length < key_len + 3) {
    return nullptr;
  }
  for (size_t i = 0; i + key_len + 2 <= length; ++i) {
    if (body[i] != '"' ||
        std::strncmp(body + i + 1, key, key_len) != 0) {
      continue;
    }
    const size_t after = i + 1 + key_len;
    if (after >= length || body[after] != '"') {
      continue;
    }
    size_t j = after + 1;
    while (j < length && (body[j] == ' ' || body[j] == '\t')) {
      ++j;
    }
    if (j >= length || body[j] != ':') {
      continue;
    }
    ++j;
    while (j < length && (body[j] == ' ' || body[j] == '\t')) {
      ++j;
    }
    return j < length ? body + j : nullptr;
  }
  return nullptr;
}

bool parseBoolField(const char* body, size_t length, const char* key,
                    bool& out) {
  const char* v = findValue(body, length, key);
  if (v == nullptr) {
    return false;
  }
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

bool parseFloatField(const char* body, size_t length, const char* key,
                     float& out) {
  const char* v = findValue(body, length, key);
  if (v == nullptr) {
    return false;
  }
  char* end = nullptr;
  const double parsed = std::strtod(v, &end);
  if (end == v) {
    return false;
  }
  out = static_cast<float>(parsed);
  return true;
}

bool parseUintField(const char* body, size_t length, const char* key,
                    uint32_t& out) {
  const char* v = findValue(body, length, key);
  if (v == nullptr || *v < '0' || *v > '9') {
    return false;
  }
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(v, &end, 10);
  if (end == v) {
    return false;
  }
  out = static_cast<uint32_t>(parsed);
  return true;
}

void buildUrl(char* out, size_t out_size, const char* suffix) {
  std::snprintf(out, out_size, "%s%s", cloud_config::API_BASE_URL, suffix);
}

}  // namespace

void CloudClient::begin() {
  input_ = CloudControlInput{};
  last_upload_ms_ = 0;
  last_video_upload_ms_ = 0;
  last_poll_ms_ = 0;
  upload_seq_ = 0;
  if (cloud_config::ENABLED) {
    FB_LOGI("cloud_client: enabled device=%s", cloud_config::DEVICE_ID);
  }
}

bool CloudClient::configured() const {
  return cloud_config::ENABLED && hasText(cloud_config::API_BASE_URL) &&
         hasText(cloud_config::DEVICE_ID) && hasText(cloud_config::DEVICE_TOKEN);
}

void CloudClient::update(const SystemState& state, uint32_t now_ms) {
  if (!configured()) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    markDisconnected();
    return;
  }
  if (elapsedMs(now_ms, last_poll_ms_) >= cloud_config::COMMAND_POLL_INTERVAL_MS) {
    last_poll_ms_ = now_ms;
    pollCommand(now_ms);
  }
  if (elapsedMs(now_ms, last_upload_ms_) >= cloud_config::UPLOAD_INTERVAL_MS) {
    last_upload_ms_ = now_ms;
    uploadTelemetry(state, now_ms);
  }
  if (cloud_config::VIDEO_ENABLED &&
      elapsedMs(now_ms, last_video_upload_ms_) >=
          cloud_config::VIDEO_UPLOAD_INTERVAL_MS) {
    last_video_upload_ms_ = now_ms;
    uploadCameraFrame(now_ms);
  }
}

CloudControlInput CloudClient::pollInput(uint32_t now_ms) {
  CloudControlInput snapshot;
  portENTER_CRITICAL(&mux_);
  if (input_.connected &&
      isStale(now_ms, input_.last_update_ms, profile::CLOUD_LOST_STOP_MS)) {
    stopMotion();
  }
  snapshot = input_;
  input_.safe_idle_request = false;
  portEXIT_CRITICAL(&mux_);
  return snapshot;
}

void CloudClient::stopMotion() {
  input_.unlock_request = false;
  input_.throttle = 0.0f;
  input_.steering = 0.0f;
}

void CloudClient::markDisconnected() {
  portENTER_CRITICAL(&mux_);
  input_.connected = false;
  stopMotion();
  portEXIT_CRITICAL(&mux_);
}

bool CloudClient::applyCommandBody(const char* body, size_t length,
                                   uint32_t now_ms) {
  if (body == nullptr || length == 0 ||
      std::strstr(body, "\"ok\":false") != nullptr) {
    return false;
  }

  uint32_t seq = 0;
  if (!parseUintField(body, length, "seq", seq)) {
    return false;
  }

  bool safe_idle = false;
  parseBoolField(body, length, "safe_idle", safe_idle);

  bool deadman = false;
  parseBoolField(body, length, "deadman", deadman);

  float forward = 0.0f;
  float turn = 0.0f;
  parseFloatField(body, length, "forward", forward);
  parseFloatField(body, length, "turn", turn);

  portENTER_CRITICAL(&mux_);
  if (seq == input_.last_seq) {
    // Same command as last poll: keep the cloud link fresh without reapplying
    // motion. If the server-side deadman expires, the next GET returns a new
    // stop seq and clears motion through the normal path.
    input_.connected = true;
    input_.last_update_ms = now_ms;
    portEXIT_CRITICAL(&mux_);
    return false;
  }
  if (seq < input_.last_seq) {
    // Server restarted and its in-memory seq reset. Accept and re-anchor;
    // refusing here would silently ignore every command until a reboot.
    FB_LOGW("cloud_client: seq reset %lu -> %lu (server restart?)",
            static_cast<unsigned long>(input_.last_seq),
            static_cast<unsigned long>(seq));
  }
  input_.last_seq = seq;
  input_.connected = true;
  input_.last_update_ms = now_ms;

  if (safe_idle) {
    input_.safe_idle_request = true;
    stopMotion();
  } else if (deadman) {
    input_.safe_idle_request = false;
    input_.unlock_request = true;
    input_.throttle = clampUnit(forward);
    input_.steering = clampUnit(turn);
  } else {
    input_.safe_idle_request = false;
    stopMotion();
  }
  portEXIT_CRITICAL(&mux_);
  return true;
}

void CloudClient::pollCommand(uint32_t now_ms) {
  uint32_t last_seq = 0;
  portENTER_CRITICAL(&mux_);
  last_seq = input_.last_seq;
  portEXIT_CRITICAL(&mux_);

  char suffix[192];
  std::snprintf(suffix, sizeof(suffix),
                "/api/device/%s/command?token=%s&last_seq=%lu",
                cloud_config::DEVICE_ID, cloud_config::DEVICE_TOKEN,
                static_cast<unsigned long>(last_seq));
  char url[320];
  buildUrl(url, sizeof(url), suffix);

  HTTPClient http;
  http.setTimeout(cloud_config::HTTP_TIMEOUT_MS);
  if (!http.begin(url)) {
    markDisconnected();
    return;
  }
  const int code = http.GET();
  if (code == HTTP_CODE_OK) {
    char body[kCommandBodySize];
    const String payload = http.getString();
    std::snprintf(body, sizeof(body), "%s", payload.c_str());
    applyCommandBody(body, std::strlen(body), now_ms);
  } else if (code < 0) {
    markDisconnected();
  }
  http.end();
}

void CloudClient::uploadTelemetry(const SystemState& state, uint32_t /*now_ms*/) {
  // Static: ~8 KB combined would overflow the comm task stack. Safe because
  // CloudClient is a singleton and uploadTelemetry runs only in the comm task.
  static char state_json[kStateJsonSize];
  static char logs_json[kLogsJsonSize];
  static char payload[kPayloadSize];

  const size_t state_len =
      buildStateJson(state, state_json, sizeof(state_json));
  if (state_len == 0) {
    return;
  }
  if (DebugConsole::drainRecentJson(logs_json, sizeof(logs_json)) == 0) {
    std::snprintf(logs_json, sizeof(logs_json), "[]");
  }

  const int written = std::snprintf(
      payload, sizeof(payload),
      "{\"device_id\":\"%s\",\"token\":\"%s\",\"seq\":%lu,"
      "\"state\":%s,\"logs\":%s}",
      cloud_config::DEVICE_ID, cloud_config::DEVICE_TOKEN,
      static_cast<unsigned long>(++upload_seq_), state_json, logs_json);
  if (written < 0 || static_cast<size_t>(written) >= sizeof(payload)) {
    FB_LOGW("cloud_client: telemetry payload too large");
    return;
  }

  char suffix[160];
  std::snprintf(suffix, sizeof(suffix), "/api/device/%s/ingest",
                cloud_config::DEVICE_ID);
  char url[288];
  buildUrl(url, sizeof(url), suffix);

  HTTPClient http;
  http.setTimeout(cloud_config::HTTP_TIMEOUT_MS);
  if (!http.begin(url)) {
    return;
  }
  http.addHeader("Content-Type", "application/json");
  const int code = http.POST(reinterpret_cast<uint8_t*>(payload),
                             static_cast<size_t>(written));
  if (code < 0) {
    FB_LOGW("cloud_client: upload failed code=%d", code);
  }
  http.end();
}

void CloudClient::uploadCameraFrame(uint32_t /*now_ms*/) {
  HTTPClient capture;
  capture.setTimeout(cloud_config::VIDEO_HTTP_TIMEOUT_MS);
  if (!capture.begin(cloud_config::CAMERA_CAPTURE_URL)) {
    return;
  }

  const int code = capture.GET();
  if (code != HTTP_CODE_OK) {
    capture.end();
    return;
  }

  const int content_len = capture.getSize();
  if (content_len <= 0 ||
      static_cast<uint32_t>(content_len) > cloud_config::VIDEO_MAX_FRAME_BYTES) {
    FB_LOGW("cloud_client: camera frame size invalid len=%d", content_len);
    capture.end();
    return;
  }

  uint8_t* frame = static_cast<uint8_t*>(
      heap_caps_malloc(content_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (frame == nullptr) {
    frame = static_cast<uint8_t*>(heap_caps_malloc(content_len, MALLOC_CAP_8BIT));
  }
  if (frame == nullptr) {
    FB_LOGW("cloud_client: no memory for camera frame len=%d", content_len);
    capture.end();
    return;
  }

  WiFiClient* stream = capture.getStreamPtr();
  size_t read_len = 0;
  const uint32_t deadline_ms = millis() + cloud_config::VIDEO_HTTP_TIMEOUT_MS;
  while (read_len < static_cast<size_t>(content_len) && millis() < deadline_ms) {
    const int available = stream->available();
    if (available <= 0) {
      delay(2);
      continue;
    }
    const size_t want =
        std::min(static_cast<size_t>(available),
                 static_cast<size_t>(content_len) - read_len);
    const int got = stream->read(frame + read_len, want);
    if (got > 0) {
      read_len += static_cast<size_t>(got);
    }
  }
  capture.end();

  if (read_len != static_cast<size_t>(content_len) || read_len < 16 ||
      frame[0] != 0xFF || frame[1] != 0xD8) {
    FB_LOGW("cloud_client: incomplete/invalid camera frame read=%u len=%d",
            static_cast<unsigned>(read_len), content_len);
    heap_caps_free(frame);
    return;
  }

  char suffix[192];
  std::snprintf(suffix, sizeof(suffix), "/api/device/%s/video/upload?token=%s",
                cloud_config::DEVICE_ID, cloud_config::DEVICE_TOKEN);
  char url[320];
  buildUrl(url, sizeof(url), suffix);

  HTTPClient upload;
  upload.setTimeout(cloud_config::VIDEO_HTTP_TIMEOUT_MS);
  if (!upload.begin(url)) {
    heap_caps_free(frame);
    return;
  }
  upload.addHeader("Content-Type", "image/jpeg");
  const int upload_code = upload.POST(frame, read_len);
  if (upload_code < 0) {
    FB_LOGW("cloud_client: camera upload failed code=%d", upload_code);
  }
  upload.end();
  heap_caps_free(frame);
}

}  // namespace followbox
