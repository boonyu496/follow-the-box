#pragma once

#include <cstdint>

namespace followbox {

inline uint32_t elapsedMs(uint32_t now_ms, uint32_t last_ms) {
  return static_cast<uint32_t>(now_ms - last_ms);
}

inline bool isStale(uint32_t now_ms, uint32_t last_ms, uint32_t timeout_ms) {
  return last_ms == 0 || elapsedMs(now_ms, last_ms) > timeout_ms;
}

}  // namespace followbox

