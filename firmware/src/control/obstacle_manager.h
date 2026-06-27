#pragma once

#include <cstdint>

#include "core/types.h"

namespace followbox {

// Result of the obstacle limiter for one tick. Diagnostics are exposed for
// telemetry/H5 but never feed back into the motion path.
struct ObstacleDecision {
  MotionIntent intent;          // forward-limited copy of the input intent
  bool stop_required = false;   // front obstacle inside the stop band
  float forward_scale = 1.0f;   // applied scale on positive forward [0..1]
  int front_min_mm = 0;         // closest valid front reading (0 = none)
};

// P0 obstacle limiter: front-sector slow-down and stop only.
//
// Placement (FIRMWARE-SPEC.md section 4): runs between command_pipeline and
// motion_mixer. It only attenuates *forward* motion toward an obstacle; turn
// and reverse are left untouched so the operator/follow controller can still
// steer or back away. It never writes GPIO and never bypasses the safety chain;
// SafetyManager still owns the hard gate for forward motion into the stop band,
// while this limiter adds graduated slow-down and a redundant forward cut.
//
// P0 scope (per UWB-LEGACY-MIGRATION-REVIEW task C): no automatic reverse, no
// turn injection, no avoidance state machine. Side sectors are not used yet.
class ObstacleManager {
 public:
  ObstacleDecision apply(const MotionIntent& intent,
                         const ObstacleSnapshot& obstacle) const;

 private:
  static int closestFront(const ObstacleSnapshot& obstacle);
};

}  // namespace followbox
