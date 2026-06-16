#pragma once

#include <cstdint>

#include "drive/drive_adapter_analog_bldc.h"

namespace followbox {

// NVS persistence for the throttle (PWM -> 0-5V) calibration.
//
// Per FIRMWARE-SPEC the analog throttle deadband / active window / slew limits
// must be measured per controller and stored so they survive a power cycle
// (autonomous follow stays locked out until calibrated). This store owns ONLY
// persistence + boundary validation; it never writes GPIO/PWM. The caller hands
// the loaded ThrottleCalibration to DriveAdapterAnalogBldc::setCalibration().
//
// Flash-friendly: load() runs once at boot, save() commits only when a value
// actually changed (dirty check), so steady-state operation never writes Flash.
//
// Defense in depth: load() clamps every field to a safe range before returning,
// so a blank or corrupted NVS record can never produce an out-of-range throttle
// voltage (e.g. max_mv above the 0-5V module full scale).
class CalibrationStore {
 public:
  // Open the NVS namespace. Returns false if NVS is unavailable; load() then
  // returns the compile-time defaults from profile_defaults.h.
  bool begin();

  // Read and clamp the stored throttle calibration. On a fresh device or schema
  // mismatch returns the (already safe) compile-time defaults.
  ThrottleCalibration load();

  // True when a valid calibration record was found in NVS (i.e. the unit has
  // been through throttle calibration). Valid only after load().
  bool isCalibrated() const { return calibrated_; }

  // Persist `cal` after clamping it to safe bounds. Skips the Flash write when
  // nothing changed. Returns true on success (including the no-op skip).
  bool save(const ThrottleCalibration& cal);

 private:
  // Clamp every field into a physically safe window for a 0-5V throttle module.
  static ThrottleCalibration sanitize(const ThrottleCalibration& cal);
  static bool equal(const ThrottleCalibration& a, const ThrottleCalibration& b);

  bool opened_ = false;
  bool calibrated_ = false;
  ThrottleCalibration cached_;
  bool cache_valid_ = false;
};

}  // namespace followbox
