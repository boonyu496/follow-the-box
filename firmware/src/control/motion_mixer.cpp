#include "control/motion_mixer.h"

#include <algorithm>
#include <cmath>

#include "config/profile_defaults.h"
#include "core/time_utils.h"

namespace followbox {
namespace {

float clampUnit(float value) {
  return std::max(-1.0f, std::min(1.0f, value));
}

}  // namespace

MotorCommand MotionMixer::mix(const MotionIntent& intent, float max_speed_scale,
                              uint32_t now_ms) {
  MotorCommand command;
  if (!intent.request_motion || intent.source == ControlSource::NONE ||
      max_speed_scale <= 0.0f) {
    reset();
    return command;
  }

  float left = clampUnit(intent.forward + intent.turn);
  float right = clampUnit(intent.forward - intent.turn);
  const float peak = std::max(std::fabs(left), std::fabs(right));
  if (peak > 1.0f) {
    left /= peak;
    right /= peak;
  }

  left *= std::max(0.0f, std::min(1.0f, max_speed_scale));
  right *= std::max(0.0f, std::min(1.0f, max_speed_scale));

  if (!initialized_) {
    initialized_ = true;
    last_update_ms_ = now_ms;
  }

  const uint32_t dt_ms = elapsedMs(now_ms, last_update_ms_);
  left = slew(left, last_left_, dt_ms);
  right = slew(right, last_right_, dt_ms);
  last_left_ = left;
  last_right_ = right;
  last_update_ms_ = now_ms;

  command.enable = true;
  command.brake = false;
  command.left_target = left;
  command.right_target = right;
  command.left_reverse = left < 0.0f;
  command.right_reverse = right < 0.0f;
  return command;
}

void MotionMixer::reset() {
  initialized_ = false;
  last_update_ms_ = 0;
  last_left_ = 0.0f;
  last_right_ = 0.0f;
}

float MotionMixer::slew(float target, float previous, uint32_t elapsed_ms) const {
  const float dt_s = static_cast<float>(elapsed_ms) / 1000.0f;
  const float limit_per_s = std::fabs(target) > std::fabs(previous)
                                ? profile::THROTTLE_SLEW_RISE_PER_S
                                : profile::THROTTLE_SLEW_FALL_PER_S;
  const float max_delta = limit_per_s * dt_s;
  const float delta = target - previous;
  if (delta > max_delta) {
    return previous + max_delta;
  }
  if (delta < -max_delta) {
    return previous - max_delta;
  }
  return target;
}

}  // namespace followbox

