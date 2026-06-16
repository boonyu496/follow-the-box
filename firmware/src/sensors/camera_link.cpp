#include "sensors/camera_link.h"

#include "config/profile_defaults.h"
#include "core/time_utils.h"

namespace followbox {

void CameraLink::reset() {
  status_ = CameraStatus{};
  stats_ = CameraLinkStats{};
}

void CameraLink::noteHeartbeat(uint32_t now_ms) {
  stats_.heartbeat_count++;
  stats_.last_heartbeat_ms = now_ms;
  status_.last_update_ms = now_ms;
  if (!status_.online) {
    status_.online = true;
    stats_.online_transitions++;
  }
}

void CameraLink::update(uint32_t now_ms) {
  const bool alive = !isStale(now_ms, status_.last_update_ms,
                              profile::CAMERA_LINK_STALE_TIMEOUT_MS);
  if (status_.online && !alive) {
    status_.online = false;
    stats_.offline_transitions++;
  }
}

}  // namespace followbox
