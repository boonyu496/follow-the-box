#pragma once

#include <cstdint>

#include "core/types.h"

namespace followbox {

class MotionMixer {
 public:
  MotorCommand mix(const MotionIntent& intent, float max_speed_scale, uint32_t now_ms);
  void reset();

 private:
  float slew(float target, float previous, uint32_t elapsed_ms) const;

  bool initialized_ = false;
  uint32_t last_update_ms_ = 0;
  float last_left_ = 0.0f;
  float last_right_ = 0.0f;
};

}  // namespace followbox

