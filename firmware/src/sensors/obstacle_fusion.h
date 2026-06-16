#pragma once

#include <cstdint>

#include "core/types.h"

namespace followbox {

// Pure-logic obstacle fusion: combine the lidar sweep with the forward TOF array
// and the side ultrasonic pair into a single ObstacleSnapshot.
//
// Safety rule (conservative): per sector the *closest* valid reading wins, so any
// sensor that sees something near triggers the downstream slow-down / stop. A
// sensor with no valid reading for a sector contributes nothing (it can never
// make a sector look clearer than another sensor reports). The result keeps the
// project's "0 = no valid reading" convention that SafetyManager::hasStopObstacle
// and ObstacleManager already rely on, so the motion path is unchanged - it just
// sees a richer, fused snapshot.
//
// Sector mapping:
//   front_left / front_center / front_right  <- lidar front  +  TOF (SD1/SD0/SD2)
//   side_left  / side_right                  <- lidar side   +  ultrasonic L/R
//
// The fused snapshot is valid when any contributing sensor is valid. This is a
// snapshot transform only: no GPIO, no motion, fully unit-testable.
ObstacleSnapshot fuseObstacles(const ObstacleSnapshot& lidar,
                               const TofSnapshot& tof,
                               const UltrasonicSnapshot& ultrasonic);

}  // namespace followbox
