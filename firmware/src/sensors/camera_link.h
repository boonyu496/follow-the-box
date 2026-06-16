#pragma once

#include <cstdint>

#include "core/types.h"

namespace followbox {

// Diagnostics for the ESP32-S3-CAM video link. Telemetry/H5 only.
struct CameraLinkStats {
  uint32_t heartbeat_count = 0;
  uint32_t online_transitions = 0;
  uint32_t offline_transitions = 0;
  uint32_t last_heartbeat_ms = 0;
};

// ESP32-S3-CAM link tracker.
//
// FollowBox runs the camera on a *separate* ESP32-S3-CAM board that only streams
// video; per FIRMWARE-SPEC it is never a safety input. The main controller does
// not parse video - it only tracks whether the camera link is alive so the H5
// panel can show an online/offline indicator. Whoever observes the camera (the
// web/comm layer probing the stream, or an explicit status ping) calls
// noteHeartbeat(); update() applies the staleness timeout. This class is pure
// logic: no GPIO, no UART, no I2C, and a dead link can never gate motion.
class CameraLink {
 public:
  void reset();

  // Record evidence the camera link is alive (e.g. a successful stream probe).
  void noteHeartbeat(uint32_t now_ms);

  // Apply the staleness timeout; flips the status offline when probes stop.
  void update(uint32_t now_ms);

  const CameraStatus& status() const { return status_; }
  const CameraLinkStats& stats() const { return stats_; }

 private:
  CameraStatus status_;
  CameraLinkStats stats_;
};

}  // namespace followbox
