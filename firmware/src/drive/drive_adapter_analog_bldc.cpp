#include "drive/drive_adapter_analog_bldc.h"

#include <algorithm>
#include <cmath>

#include "config/board_pins.h"
#include "core/math_utils.h"
#include "core/time_utils.h"

namespace followbox {
namespace {

constexpr int kLeftThrottlePwmChannel = 0;
constexpr int kRightThrottlePwmChannel = 1;

int absCommandToMillivolts(float target_abs, const ThrottleCalibration& calibration) {
  const float command = clampFloat(std::fabs(target_abs), 0.0f, 1.0f);
  if (nearlyZero(command)) {
    return 0;
  }
  const float span =
      static_cast<float>(calibration.max_mv - calibration.min_active_mv);
  const int mapped =
      calibration.min_active_mv + static_cast<int>(std::lround(command * span));
  return std::max(calibration.deadband_mv, std::min(calibration.max_mv, mapped));
}

}  // namespace

DriveAdapterAnalogBldc::DriveAdapterAnalogBldc()
    : left_pwm_(pins::PIN_LEFT_THROTTLE_PWM, kLeftThrottlePwmChannel,
                profile::THROTTLE_PWM_FREQUENCY_HZ,
                profile::THROTTLE_PWM_RESOLUTION_BITS),
      right_pwm_(pins::PIN_RIGHT_THROTTLE_PWM, kRightThrottlePwmChannel,
                 profile::THROTTLE_PWM_FREQUENCY_HZ,
                 profile::THROTTLE_PWM_RESOLUTION_BITS),
      brake_(pins::PIN_BRAKE_OUT, ActiveLevel::ACTIVE_HIGH),
      left_reverse_(pins::PIN_LEFT_REVERSE_OUT, ActiveLevel::ACTIVE_HIGH),
      right_reverse_(pins::PIN_RIGHT_REVERSE_OUT, ActiveLevel::ACTIVE_HIGH),
      drive_enable_(pins::PIN_DRIVE_ENABLE_OUT, ActiveLevel::ACTIVE_HIGH) {}

bool DriveAdapterAnalogBldc::begin() {
  left_pwm_.begin();
  right_pwm_.begin();
  left_reverse_.begin(false);
  right_reverse_.begin(false);
  drive_enable_.begin(false);
  brake_.begin(true);
  stopNow();
  initialized_ = true;
  last_update_ms_ = 0;
  return left_pwm_.isValid() && right_pwm_.isValid();
}

void DriveAdapterAnalogBldc::writeCommand(const MotorCommand& command,
                                          uint32_t now_ms) {
  if (!initialized_) {
    begin();
  }

  if (!command.enable || command.brake) {
    stopNow();
    last_update_ms_ = now_ms;
    return;
  }

  if (last_update_ms_ == 0) {
    last_update_ms_ = now_ms;
  }

  const uint32_t dt_ms = elapsedMs(now_ms, last_update_ms_);
  const int left_target_mv =
      applySlew(targetMillivolts(command.left_target, command.enable, command.brake),
                last_left_mv_, dt_ms);
  const int right_target_mv =
      applySlew(targetMillivolts(command.right_target, command.enable, command.brake),
                last_right_mv_, dt_ms);

  drive_enable_.writeActive(true);
  brake_.writeActive(false);
  left_reverse_.writeActive(command.left_reverse);
  right_reverse_.writeActive(command.right_reverse);
  writeThrottleMillivolts(left_target_mv, right_target_mv);

  last_left_mv_ = left_target_mv;
  last_right_mv_ = right_target_mv;
  last_update_ms_ = now_ms;
}

void DriveAdapterAnalogBldc::stopNow() {
  writeThrottleMillivolts(0, 0);
  left_reverse_.writeActive(false);
  right_reverse_.writeActive(false);
  drive_enable_.writeActive(false);
  brake_.writeActive(true);
  last_left_mv_ = 0;
  last_right_mv_ = 0;
}

int DriveAdapterAnalogBldc::targetMillivolts(float target_abs, bool enabled,
                                             bool brake) const {
  if (!enabled || brake) {
    return 0;
  }
  return absCommandToMillivolts(target_abs, calibration_);
}

int DriveAdapterAnalogBldc::applySlew(int target_mv, int previous_mv,
                                      uint32_t elapsed_ms) const {
  const int delta = target_mv - previous_mv;
  const int limit_per_s = delta > 0 ? calibration_.rise_mv_per_s
                                    : calibration_.fall_mv_per_s;
  const int max_delta =
      static_cast<int>((static_cast<int64_t>(limit_per_s) * elapsed_ms) / 1000);
  if (max_delta <= 0) {
    return previous_mv;
  }
  if (delta > max_delta) {
    return previous_mv + max_delta;
  }
  if (delta < -max_delta) {
    return previous_mv - max_delta;
  }
  return target_mv;
}

uint32_t DriveAdapterAnalogBldc::dutyFromMillivolts(
    int millivolts, const PwmOutput& output) const {
  const int safe_mv =
      std::max(0, std::min(calibration_.module_full_scale_mv, millivolts));
  const uint64_t numerator =
      static_cast<uint64_t>(safe_mv) * static_cast<uint64_t>(output.maxDuty());
  return static_cast<uint32_t>(
      numerator / static_cast<uint64_t>(calibration_.module_full_scale_mv));
}

void DriveAdapterAnalogBldc::writeThrottleMillivolts(int left_mv, int right_mv) {
  left_pwm_.writeDuty(dutyFromMillivolts(left_mv, left_pwm_));
  right_pwm_.writeDuty(dutyFromMillivolts(right_mv, right_pwm_));
}

}  // namespace followbox
