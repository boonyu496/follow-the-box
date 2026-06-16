#pragma once

#include <algorithm>
#include <cmath>

namespace followbox {

inline float clampFloat(float value, float low, float high) {
  return std::max(low, std::min(high, value));
}

inline float clampUnit(float value) {
  return clampFloat(value, -1.0f, 1.0f);
}

inline bool nearlyZero(float value, float epsilon = 0.0001f) {
  return std::fabs(value) <= epsilon;
}

}  // namespace followbox

