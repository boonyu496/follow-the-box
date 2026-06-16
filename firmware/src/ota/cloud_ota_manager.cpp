#include "ota/cloud_ota_manager.h"

#include <Arduino.h>
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

constexpr size_t kManifestBodySize = 768;

bool hasText(const char* s) {
  return s != nullptr && s[0] != '\0';
}

const char* findValue(const char* body, size_t length, const char* key) {
  const size_t key_len = std::strlen(key);
  for (size_t i = 0; i + key_len + 2 <= length; ++i) {
    if (body[i] != '"' || std::strncmp(body + i + 1, key, key_len) != 0) {
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

bool parseBoolField(const char* body, size_t length, const char* key, bool& out) {
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

bool parseIntField(const char* body, size_t length, const char* key, int& out) {
  const char* v = findValue(body, length, key);
  if (v == nullptr || *v < '0' || *v > '9') {
    return false;
  }
  out = std::atoi(v);
  return true;
}

bool parseStringField(const char* body, size_t length, const char* key,
                      char* out, size_t out_size) {
  const char* v = findValue(body, length, key);
  if (v == nullptr || *v != '"' || out_size == 0) {
    return false;
  }
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
  in_progress_ = false;
  last_attempt_version_[0] = '\0';
}

bool CloudOtaManager::configured() const {
  return ota_config::CLOUD_OTA_ENABLED && cloud_config::ENABLED &&
         hasText(cloud_config::API_BASE_URL) && hasText(cloud_config::DEVICE_ID) &&
         hasText(cloud_config::DEVICE_TOKEN);
}

void CloudOtaManager::setInProgress(bool active) {
  in_progress_ = active;
  if (safety_callback_ != nullptr) {
    safety_callback_(active);
  }
}

void CloudOtaManager::update(uint32_t now_ms) {
  if (!configured() || in_progress_ || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (last_check_ms_ != 0 && now_ms - last_check_ms_ < ota_config::CHECK_INTERVAL_MS) {
    return;
  }
  last_check_ms_ = now_ms;

  Manifest manifest;
  if (!fetchManifest(manifest) || !manifest.valid) {
    return;
  }
  if (!manifest.force &&
      std::strcmp(manifest.version, ota_config::CURRENT_VERSION) == 0) {
    return;
  }
  if (!manifest.force &&
      std::strcmp(manifest.version, last_attempt_version_) == 0) {
    return;
  }
  std::snprintf(last_attempt_version_, sizeof(last_attempt_version_), "%s",
                manifest.version);

  char reason[80] = "ok";
  const bool ok = performUpdate(manifest, reason, sizeof(reason));
  reportResult(manifest.version, ok, reason);
  if (ok) {
    FB_LOGI("cloud_ota: update complete version=%s, restarting",
            manifest.version);
    delay(500);
    ESP.restart();
  }
}

bool CloudOtaManager::fetchManifest(Manifest& manifest) {
  char url[360];
  std::snprintf(url, sizeof(url),
                "%s/api/device/%s/firmware/version?token=%s&current=%s",
                cloud_config::API_BASE_URL, cloud_config::DEVICE_ID,
                cloud_config::DEVICE_TOKEN, ota_config::CURRENT_VERSION);

  HTTPClient http;
  http.setTimeout(ota_config::HTTP_TIMEOUT_MS);
  if (!http.begin(url)) {
    return false;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return false;
  }
  const String payload = http.getString();
  http.end();

  char body[kManifestBodySize];
  std::snprintf(body, sizeof(body), "%s", payload.c_str());
  const size_t length = std::strlen(body);

  bool ok = false;
  parseBoolField(body, length, "ok", ok);
  parseBoolField(body, length, "force", manifest.force);
  parseIntField(body, length, "size", manifest.size);
  const bool has_version =
      parseStringField(body, length, "version", manifest.version,
                       sizeof(manifest.version));
  const bool has_url =
      parseStringField(body, length, "url", manifest.url, sizeof(manifest.url));
  parseStringField(body, length, "md5", manifest.md5, sizeof(manifest.md5));

  manifest.valid = ok && has_version && has_url;
  return manifest.valid;
}

bool CloudOtaManager::performUpdate(const Manifest& manifest, char* reason,
                                    size_t reason_size) {
  char url[360];
  joinUrl(url, sizeof(url), manifest.url);

  setInProgress(true);
  FB_LOGW("cloud_ota: start version=%s url=%s", manifest.version, url);

  HTTPClient http;
  http.setTimeout(ota_config::HTTP_TIMEOUT_MS);
  if (!http.begin(url)) {
    std::snprintf(reason, reason_size, "bad download url");
    setInProgress(false);
    return false;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    std::snprintf(reason, reason_size, "download http %d", code);
    http.end();
    setInProgress(false);
    return false;
  }

  const int content_length = http.getSize();
  const int expected_size = manifest.size > 0 ? manifest.size : content_length;
  if (expected_size <= 0) {
    std::snprintf(reason, reason_size, "unknown size");
    http.end();
    setInProgress(false);
    return false;
  }

  if (!Update.begin(static_cast<size_t>(expected_size), U_FLASH)) {
    std::snprintf(reason, reason_size, "update begin failed");
    http.end();
    setInProgress(false);
    return false;
  }
  if (manifest.md5[0] != '\0') {
    Update.setMD5(manifest.md5);
  }

  WiFiClient* stream = http.getStreamPtr();
  const size_t written = Update.writeStream(*stream);
  const bool ended = Update.end();
  const bool finished = Update.isFinished();
  http.end();

  if (!ended || !finished ||
      written != static_cast<size_t>(expected_size)) {
    std::snprintf(reason, reason_size, "write failed err=%u",
                  static_cast<unsigned>(Update.getError()));
    Update.abort();
    setInProgress(false);
    return false;
  }

  std::snprintf(reason, reason_size, "ok");
  return true;
}

void CloudOtaManager::reportResult(const char* version, bool ok,
                                   const char* reason) {
  char url[300];
  std::snprintf(url, sizeof(url), "%s/api/device/%s/ota-result",
                cloud_config::API_BASE_URL, cloud_config::DEVICE_ID);

  char payload[256];
  std::snprintf(payload, sizeof(payload),
                "{\"token\":\"%s\",\"version\":\"%s\",\"ok\":%s,"
                "\"reason\":\"%s\"}",
                cloud_config::DEVICE_TOKEN, version, ok ? "true" : "false",
                reason != nullptr ? reason : "");

  HTTPClient http;
  http.setTimeout(cloud_config::HTTP_TIMEOUT_MS);
  if (http.begin(url)) {
    http.addHeader("Content-Type", "application/json");
    http.POST(reinterpret_cast<uint8_t*>(payload), std::strlen(payload));
  }
  http.end();
}

}  // namespace followbox
