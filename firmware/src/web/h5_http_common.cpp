#include "web/h5_http_common.h"

#include <Arduino.h>

#include <cstring>

#include "config/network_config.h"

namespace followbox {
namespace {

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

}  // namespace

const char kAckOk[] = "{\"ok\":true}";
const char kAckRejected[] = "{\"ok\":false}";
const char kAckUnauthorized[] = "{\"ok\":false,\"reason\":\"unauthorized\"}";

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

}  // namespace followbox
