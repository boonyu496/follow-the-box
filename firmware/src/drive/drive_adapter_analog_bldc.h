#pragma once

#include <cstdint>

#include "config/profile_defaults.h"
#include "core/types.h"
#include "drive/drive_adapter.h"
#include "hal/gpio_out.h"
#include "hal/pwm_output.h"

namespace followbox {

struct ThrottleCalibration {
  int deadband_mv = profile::THROTTLE_DEADBAND_MV;
  int min_active_mv = profile::THROTTLE_MIN_ACTIVE_MV;
  int max_mv = profile::THROTTLE_MAX_MV;
  int module_full_scale_mv = profile::THROTTLE_MODULE_FULL_SCALE_MV;
  int rise_mv_per_s = 800;
  int fall_mv_per_s = 1600;
};

class DriveAdapterAnalogBldc : public DriveAdapter {
 public:
  DriveAdapterAnalogBldc();

  bool begin() override;
  void writeCommand(const MotorCommand& command, uint32_t now_ms) override;
  void stopNow() override;

  // Inject a persisted/measured throttle calibration (from CalibrationStore).
  // Only changes the mV mapping + slew limits; the output stays gated downstream
  // (writeCommand still honours enable/brake). Safe to call before begin().
  void setCalibration(const ThrottleCalibration& calibration) {
    calibration_ = calibration;
  }

 private:
  int targetMillivolts(float target_abs, bool enabled, bool brake) const;
  int applySlew(int target_mv, int previous_mv, uint32_t elapsed_ms) const;
  uint32_t dutyFromMillivolts(int millivolts, const PwmOutput& output) const;
  void writeThrottleMillivolts(int left_mv, int right_mv);

  PwmOutput left_pwm_;
  PwmOutput right_pwm_;
  GpioOut brake_;
  GpioOut left_reverse_;
  GpioOut right_reverse_;
  GpioOut drive_enable_;
  ThrottleCalibration calibration_;
  bool initialized_ = false;
  uint32_t last_update_ms_ = 0;
  int last_left_mv_ = 0;
  int last_right_mv_ = 0;
};

}  // namespace followbox

