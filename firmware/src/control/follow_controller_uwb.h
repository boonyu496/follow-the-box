#pragma once

#include <cstdint>

#include "core/types.h"

namespace followbox {

// Stateless-per-tick UWB follow policy. Translates a filtered UwbTarget (plus
// optional IMU damping) into a MotionIntent. It NEVER touches GPIO/PWM, never
// calls a drive adapter, and never bypasses the safety chain: every intent it
// produces still passes through SafetyManager::applyFinalGate() downstream.
//
// Borrowed strategy (not code) from the legacy UWB car:
//   - flatten the 3D UWB range using the handheld tag height,
//   - stop band with hysteresis so the box does not creep when parked,
//   - reduce forward speed when bearing error is large (turn first),
//   - optional IMU yaw-rate damping to limit head-swing.
class FollowControllerUwb {
 public:
  void reset();

  // Produces an intent for the current tick. When the UWB target is invalid the
  // returned intent requests no motion (forward=turn=0); the safety manager is
  // still responsible for the authoritative UWB-lost stop.
  MotionIntent update(const UwbTarget& uwb, const ImuSnapshot& imu,
                      uint32_t now_ms);

 private:
  bool stopped_near_ = false;  // hysteresis latch for the near-stop band
};

}  // namespace followbox
