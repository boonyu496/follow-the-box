#pragma once

#include <cstdint>

namespace followbox {

constexpr uint32_t kTimestampFutureSlackMs = 50;

inline uint32_t elapsedMs(uint32_t now_ms, uint32_t last_ms) {
  return static_cast<uint32_t>(now_ms - last_ms);
}

inline bool isNearFutureTimestamp(uint32_t now_ms, uint32_t last_ms) {
  return last_ms > now_ms &&
         static_cast<uint32_t>(last_ms - now_ms) <= kTimestampFutureSlackMs;
}

inline uint32_t elapsedMsClamped(uint32_t now_ms, uint32_t last_ms) {
  if (last_ms == 0) {
    return 0;
  }
  if (isNearFutureTimestamp(now_ms, last_ms)) {
    return 0;
  }
  return elapsedMs(now_ms, last_ms);
}

inline bool isStale(uint32_t now_ms, uint32_t last_ms, uint32_t timeout_ms) {
  if (last_ms == 0) {
    return true;
  }
  if (isNearFutureTimestamp(now_ms, last_ms)) {
    return false;
  }
  return elapsedMs(now_ms, last_ms) > timeout_ms;
}

}  // namespace followbox

