#include "sensors/obstacle_fusion.h"

#include <algorithm>

namespace followbox {
namespace {

// Closest of two sector readings using the project's "<= 0 means no reading"
// convention. Returns 0 only when neither sensor has a valid reading.
int closestPositive(int a, int b) {
  if (a <= 0) {
    return b > 0 ? b : 0;
  }
  if (b <= 0) {
    return a;
  }
  return std::min(a, b);
}

// A TOF / ultrasonic channel only contributes when its own valid flag is set.
int gated(bool valid, int mm) { return valid ? mm : 0; }

}  // namespace

ObstacleSnapshot fuseObstacles(const ObstacleSnapshot& lidar,
                               const TofSnapshot& tof,
                               const UltrasonicSnapshot& ultrasonic) {
  ObstacleSnapshot fused;
  fused.valid = lidar.valid || tof.valid || ultrasonic.valid;

  // Freshness reflects the most recent contributing sensor (informational only;
  // each source already applied its own staleness before we get here).
  fused.last_update_ms = std::max(
      {lidar.last_update_ms, tof.last_update_ms, ultrasonic.last_update_ms});

  // Front sectors: lidar sweep + forward TOF array (closest wins).
  const int lidar_fl = lidar.valid ? lidar.front_left_mm : 0;
  const int lidar_fc = lidar.valid ? lidar.front_center_mm : 0;
  const int lidar_fr = lidar.valid ? lidar.front_right_mm : 0;
  fused.front_left_mm =
      closestPositive(lidar_fl, gated(tof.front_left_valid, tof.front_left_mm));
  fused.front_center_mm = closestPositive(
      lidar_fc, gated(tof.front_center_valid, tof.front_center_mm));
  fused.front_right_mm = closestPositive(
      lidar_fr, gated(tof.front_right_valid, tof.front_right_mm));

  // Side sectors: lidar sweep + side ultrasonic pair (closest wins).
  const int lidar_sl = lidar.valid ? lidar.side_left_mm : 0;
  const int lidar_sr = lidar.valid ? lidar.side_right_mm : 0;
  fused.side_left_mm =
      closestPositive(lidar_sl, gated(ultrasonic.left_valid, ultrasonic.left_mm));
  fused.side_right_mm = closestPositive(
      lidar_sr, gated(ultrasonic.right_valid, ultrasonic.right_mm));

  return fused;
}

}  // namespace followbox
