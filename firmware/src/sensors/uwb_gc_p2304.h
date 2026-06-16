#pragma once

#include <cstdint>

#include "core/types.h"

namespace followbox {

// Diagnostics for the GC-P2304-GS-2 binary frame stream. Exposed to telemetry/H5
// for bring-up without ever feeding back into the motion path.
struct UwbParserStats {
  uint32_t rx_byte_count = 0;
  uint32_t frame_count = 0;
  uint32_t parse_error_count = 0;
  uint32_t last_frame_ms = 0;
  uint16_t last_raw_distance_cm = 0;
  int16_t last_raw_angle_deg = 0;
  int last_rssi_dbm = 0;
};

// Pure-logic parser + filter for the GC-P2304-GS-2 UWB ranging module.
//
// Frozen binary frame (protocols/UWB-GC-P2304.md):
//   F0 06 ID_L ID_H DIST_L DIST_H ANG_L ANG_H RSSI AA   (10 bytes, little-endian)
//
// Constraints honoured here:
//   - No GPIO / UART / Serial access; callers feed raw bytes via pushByte().
//   - On timeout the target is invalidated and filters reset (no stale heading).
//   - Negative-angle two's-complement encoding is assumed but MUST be confirmed
//     by left/right bring-up capture before AUTO_FOLLOW is trusted.
class UwbGcP2304Parser {
 public:
  void reset();

  // Feed one received byte. Returns true when a complete, valid frame was parsed.
  bool pushByte(uint8_t byte, uint32_t now_ms);

  // Call periodically (even with no new bytes) to apply the staleness timeout.
  void update(uint32_t now_ms);

  const UwbTarget& target() const { return target_; }
  const UwbParserStats& stats() const { return stats_; }

 private:
  void commitFrame(uint16_t distance_cm, int16_t angle_raw_deg, int rssi_dbm,
                   uint32_t now_ms);
  static float normalizeAngleDeg(float deg);
  static uint8_t rssiToConfidence(int rssi_dbm);

  UwbTarget target_;
  UwbParserStats stats_;
  uint8_t buffer_[10] = {0};
  uint8_t index_ = 0;
  bool filter_init_ = false;
  float ema_distance_cm_ = 0.0f;
  float ema_bearing_deg_ = 0.0f;
};

}  // namespace followbox
