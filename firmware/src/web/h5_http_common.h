#pragma once

#include <cstddef>
#include <cstring>

#include <ESPAsyncWebServer.h>

namespace followbox {

extern const char kAckOk[];
extern const char kAckRejected[];
extern const char kAckUnauthorized[];

bool localApiAuthorized(AsyncWebServerRequest* request);
bool requireLocalApiAuth(AsyncWebServerRequest* request);

// Accumulate a small POST body into a stack-sized static buffer, invoking fn
// once the full body has arrived. Oversized bodies fail closed.
template <typename Fn>
void onH5Body(AsyncWebServerRequest* request, uint8_t* data, size_t len,
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

}  // namespace followbox
