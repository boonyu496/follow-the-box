#include "control/follow_controller_uwb.h"

#include <algorithm>
#include <cmath>

#include "config/profile_defaults.h"
#include "core/math_utils.h"

namespace followbox {
namespace {

// Flatten the reported (slant) range using the tag height. Returns the
// horizontal distance in mm, clamped at 0 when the tag is closer than its
// height.
float horizontalDistanceMm(int slant_mm) {
  const float slant = static_cast<float>(slant_mm);
  const float height = profile::FOLLOW_TAG_HEIGHT_MM;
  const float sq = slant * slant - height * height;
  if (sq <= 0.0f) {
    return 0.0f;
  }
  return std::sqrt(sq);
}

}  // namespace

void FollowControllerUwb::reset() {
  stopped_near_ = false;
}

MotionIntent FollowControllerUwb::update(const UwbTarget& uwb,
                                         const ImuSnapshot& imu,
                                         uint32_t /*now_ms*/) {
  MotionIntent intent;
  intent.source = ControlSource::UWB_FOLLOW;

  if (!uwb.valid) {
    stopped_near_ = false;
    intent.request_motion = false;
    intent.forward = 0.0f;
    intent.turn = 0.0f;
    return intent;
  }

  intent.request_motion = true;

  const float flat_mm = horizontalDistanceMm(uwb.distance_mm);

  // Hysteresis near-stop band: latch stop below the low threshold, only release
  // once we are clearly beyond the resume threshold.
  if (flat_mm <= static_cast<float>(profile::FOLLOW_NEAR_STOP_DISTANCE_MM)) {
    stopped_near_ = true;
  } else if (flat_mm >= static_cast<float>(profile::FOLLOW_NEAR_RESUME_DISTANCE_MM)) {
    stopped_near_ = false;
  }

  // Turn: proportional to bearing error, optionally damped by IMU yaw rate.
  float turn = uwb.bearing_deg / profile::FOLLOW_TURN_FULL_SCALE_DEG;
  if (imu.valid && profile::FOLLOW_YAW_DAMP_GAIN != 0.0f) {
    turn -= imu.yaw_rate_dps * profile::FOLLOW_YAW_DAMP_GAIN;
  }
  turn = clampUnit(turn);

  // Forward: ramp from the resume distance up to the full-speed distance.
  float forward = 0.0f;
  if (!stopped_near_) {
    const float resume = static_cast<float>(profile::FOLLOW_NEAR_RESUME_DISTANCE_MM);
    const float full = static_cast<float>(profile::FOLLOW_FAR_FULL_SPEED_DISTANCE_MM);
    const float span = std::max(1.0f, full - resume);
    forward = clampFloat((flat_mm - resume) / span, 0.0f, 1.0f);

    // Turn-first: cut forward speed when the bearing error is large so the box
    // pivots toward the tag instead of charging off-axis.
    const float bearing_mag = std::fabs(uwb.bearing_deg);
    if (bearing_mag > profile::FOLLOW_BEARING_SLOW_DEG) {
      const float slow = profile::FOLLOW_BEARING_SLOW_DEG;
      const float full_scale = std::max(slow + 1.0f, profile::FOLLOW_TURN_FULL_SCALE_DEG);
      const float reduce = clampFloat(
          1.0f - (bearing_mag - slow) / (full_scale - slow), 0.0f, 1.0f);
      forward *= reduce;
    }

    forward *= profile::FOLLOW_MAX_FORWARD;
  }

  intent.forward = clampFloat(forward, 0.0f, 1.0f);
  intent.turn = turn;
  return intent;
}

}  // namespace followbox
