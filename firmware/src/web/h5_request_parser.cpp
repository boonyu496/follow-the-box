#include "web/h5_request_parser.h"

#include <cctype>
#include <cstdlib>
#include <cstring>

namespace followbox {
namespace {

// Find the value text for "key" in a flat JSON object. Returns a pointer just
// past the colon (skipping whitespace), or nullptr when the key is absent.
const char* findValue(const char* body, size_t length, const char* key) {
  const size_t key_len = std::strlen(key);
  // Need room for "key" plus a colon and a value.
  if (length < key_len + 3) {
    return nullptr;
  }
  for (size_t i = 0; i + key_len + 2 <= length; ++i) {
    if (body[i] != '"') {
      continue;
    }
    if (std::strncmp(body + i + 1, key, key_len) != 0) {
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
    return (j < length) ? body + j : nullptr;
  }
  return nullptr;
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
  if (v == nullptr || !std::isdigit(static_cast<unsigned char>(*v))) {
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

}  // namespace

JogRequest parseJogRequest(const char* body, size_t length) {
  JogRequest req;
  if (body == nullptr || length == 0) {
    return req;
  }

  // seq, forward and turn are mandatory; a request without them is rejected so
  // a malformed frame can never be replayed as motion.
  if (!parseUintField(body, length, "seq", req.seq) ||
      !parseFloatField(body, length, "forward", req.forward) ||
      !parseFloatField(body, length, "turn", req.turn)) {
    return req;
  }

  // deadman is fail-safe: absent or non-bool -> false (stop).
  if (!parseBoolField(body, length, "deadman", req.deadman)) {
    req.deadman = false;
  }

  req.valid = true;
  return req;
}

H5ModeRequest parseModeRequest(const char* body, size_t length) {
  if (body == nullptr || length == 0) {
    return H5ModeRequest::NONE;
  }
  const char* v = findValue(body, length, "requested_mode");
  if (v == nullptr || *v != '"') {
    return H5ModeRequest::NONE;
  }
  ++v;  // skip opening quote
  const size_t remaining = length - static_cast<size_t>(v - body);

  struct Entry {
    const char* name;
    size_t len;
    H5ModeRequest request;
  };
  static const Entry kEntries[] = {
      {"SAFE_IDLE", 9, H5ModeRequest::SAFE_IDLE},
      {"MANUAL_H5_LOW_SPEED", 19, H5ModeRequest::MANUAL_H5_LOW_SPEED},
      {"AUTO_FOLLOW_REQUEST", 19, H5ModeRequest::AUTO_FOLLOW_REQUEST},
  };
  for (const Entry& e : kEntries) {
    if (remaining >= e.len + 1 && std::strncmp(v, e.name, e.len) == 0 &&
        v[e.len] == '"') {
      return e.request;
    }
  }
  return H5ModeRequest::NONE;
}

bool parseResetFaultRequest(const char* body, size_t length) {
  if (body == nullptr || length == 0) {
    return false;
  }
  bool confirm = false;
  if (parseBoolField(body, length, "confirm", confirm)) {
    return confirm;
  }
  return false;
}

CalibrateRequest parseCalibrateRequest(const char* body, size_t length) {
  CalibrateRequest req;
  if (body == nullptr || length == 0) {
    return req;
  }
  if (!parseUintField(body, length, "deadband_mv", req.deadband_mv) ||
      !parseUintField(body, length, "min_active_mv", req.min_active_mv) ||
      !parseUintField(body, length, "max_mv", req.max_mv) ||
      !parseUintField(body, length, "module_full_scale_mv", req.module_full_scale_mv) ||
      !parseUintField(body, length, "rise_mv_per_s", req.rise_mv_per_s) ||
      !parseUintField(body, length, "fall_mv_per_s", req.fall_mv_per_s)) {
    return req;
  }
  req.valid = true;
  return req;
}

namespace {

// Copy a JSON string value for `key` into out (capacity out_size, always
// NUL-terminated). Returns false when the key is absent, not a string, longer
// than the buffer, or uses escapes other than \" and \\.
bool parseStringField(const char* body, size_t length, const char* key,
                      char* out, size_t out_size) {
  const char* v = findValue(body, length, key);
  if (v == nullptr || *v != '"' || out == nullptr || out_size == 0) {
    return false;
  }
  ++v;  // skip opening quote
  size_t o = 0;
  const char* end = body + length;
  while (v < end && *v != '"') {
    char c = *v;
    if (c == '\\') {
      ++v;
      if (v >= end || (*v != '"' && *v != '\\')) {
        return false;
      }
      c = *v;
    }
    if (o + 1 >= out_size) {
      return false;  // too long for the caller's buffer
    }
    out[o++] = c;
    ++v;
  }
  if (v >= end) {
    return false;  // unterminated string
  }
  out[o] = '\0';
  return true;
}

}  // namespace

WifiConfigRequest parseWifiConfigRequest(const char* body, size_t length) {
  WifiConfigRequest req;
  if (body == nullptr || length == 0) {
    return req;
  }
  if (!parseStringField(body, length, "ssid", req.ssid, sizeof(req.ssid)) ||
      req.ssid[0] == '\0') {
    return req;
  }
  // Password optional (open AP). Reject only if present but malformed/too long.
  const char* has_pass = findValue(body, length, "password");
  if (has_pass != nullptr &&
      !parseStringField(body, length, "password", req.password,
                        sizeof(req.password))) {
    return req;
  }
  // WPA2 requires 8..63 chars; an in-between length would brick the join.
  const size_t pass_len = std::strlen(req.password);
  if (pass_len > 0 && pass_len < 8) {
    return req;
  }
  req.valid = true;
  return req;
}

WizardRequest parseWizardRequest(const char* body, size_t length) {
  WizardRequest req;
  if (body == nullptr || length == 0) {
    return req;
  }
  if (!parseBoolField(body, length, "complete", req.complete)) {
    return req;
  }
  if (req.complete) {
    if (!parseBoolField(body, length, "estop_checked", req.estop_checked) ||
        !parseBoolField(body, length, "wheels_lifted", req.wheels_lifted) ||
        !parseBoolField(body, length, "direction_checked", req.direction_checked) ||
        !parseBoolField(body, length, "throttle_checked", req.throttle_checked)) {
      return req;
    }
    if (!req.estop_checked || !req.wheels_lifted || !req.direction_checked ||
        !req.throttle_checked) {
      return req;
    }
  }
  req.valid = true;
  return req;
}

}  // namespace followbox
