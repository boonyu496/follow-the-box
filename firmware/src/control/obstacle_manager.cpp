#include "control/obstacle_manager.h"

#include <algorithm>

#include "config/profile_defaults.h"
#include "core/math_utils.h"

namespace followbox {

int ObstacleManager::closestFront(const ObstacleSnapshot& obstacle) {
  const int sectors[] = {
      obstacle.front_left_mm,
      obstacle.front_center_mm,
      obstacle.front_right_mm,
  };
  int closest = 0;
  for (int distance : sectors) {
    if (distance <= 0) {
      continue;  // 0 / negative means "no valid reading" for that sector
    }
    if (closest == 0 || distance < closest) {
      closest = distance;
    }
  }
  return closest;
}

ObstacleDecision ObstacleManager::apply(const MotionIntent& intent,
                                        const ObstacleSnapshot& obstacle) const {
  ObstacleDecision decision;
  decision.intent = intent;

  // Without a valid snapshot we cannot judge obstacles. Pass the intent through
  // unchanged; the safety manager and (future) sensor heartbeat remain the
  // authority. NOTE: once the lidar/TOF task is wired, an invalid-but-expected
  // snapshot should be treated as a sensor timeout, not as "clear".
  if (!obstacle.valid) {
    return decision;
  }

  const int front_min = closestFront(obstacle);
  decision.front_min_mm = front_min;
  if (front_min <= 0) {
    return decision;  // no usable front reading
  }

  // Only attenuate forward motion *toward* the obstacle. Reverse (negative
  // forward) and turn are preserved so the box can still steer or back away.
  if (decision.intent.forward <= 0.0f) {
    return decision;
  }

  const int stop_mm = profile::OBSTACLE_STOP_DISTANCE_MM;
  const int slow_mm = profile::OBSTACLE_SLOW_DISTANCE_MM;

  if (front_min <= stop_mm) {
    decision.stop_required = true;
    decision.forward_scale = 0.0f;
    decision.intent.forward = 0.0f;
    return decision;
  }

  if (front_min < slow_mm) {
    const float span = static_cast<float>(std::max(1, slow_mm - stop_mm));
    decision.forward_scale =
        clampFloat(static_cast<float>(front_min - stop_mm) / span, 0.0f, 1.0f);
    decision.intent.forward *= decision.forward_scale;
  }

  return decision;
}

}  // namespace followbox
