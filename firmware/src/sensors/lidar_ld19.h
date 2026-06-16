#pragma once

#include <cstdint>

#include "core/types.h"

namespace followbox {

// Diagnostics for the LD19/LD06 packet stream. Exposed to telemetry only.
struct LidarStats {
  uint32_t rx_byte_count = 0;
  uint32_t packet_count = 0;
  uint32_t crc_error_count = 0;
  uint32_t point_count = 0;
  uint32_t scan_count = 0;
  uint32_t last_packet_ms = 0;
  float last_speed_dps = 0.0f;
};

// Pure-logic parser for the LDROBOT LD19 / LD06 DTOF lidar (UART 230400 8N1).
//
// Packet (47 bytes, little-endian fields):
//   [0]    header   0x54
//   [1]    ver/len  0x2C  (low 5 bits = 12 measurement points)
//   [2..3] speed    deg/s
//   [4..5] start_angle  0.01 deg
//   [6..41] 12 points x (dist_lo, dist_hi : mm; intensity)
//   [42..43] end_angle  0.01 deg
//   [44..45] timestamp  ms
//   [46]   crc8
//
// The parser folds the 360 deg scan into the project's ObstacleSnapshot sectors
// (front-left / front-center / front-right / side-left / side-right). It never
// touches GPIO/UART; callers feed raw bytes via pushByte(). Mounting orientation
// (LIDAR_MOUNT_YAW_OFFSET_DEG) and angle sign MUST be confirmed on the bench
// before the obstacle data is trusted for motion.
class LidarLd19 {
 public:
  void reset();

  // Feed one received byte. Returns true when a full, CRC-valid packet parsed.
  bool pushByte(uint8_t byte, uint32_t now_ms);

  // Apply the staleness timeout (invalidates the snapshot when the stream stops).
  void update(uint32_t now_ms);

  const ObstacleSnapshot& snapshot() const { return obstacle_; }
  const LidarStats& stats() const { return stats_; }

 private:
  static constexpr uint8_t kHeader = 0x54;
  static constexpr uint8_t kVerLen = 0x2C;
  static constexpr uint8_t kPointCount = 12;
  static constexpr uint8_t kPacketLength = 47;

  void parsePacket(uint32_t now_ms);
  void accumulatePoint(float robot_angle_deg, uint16_t distance_mm,
                       uint8_t intensity);
  void finalizeScan(uint32_t now_ms);
  void resetSectors();
  static uint8_t crc8(const uint8_t* data, uint8_t length);
  static float normalizeAngleDeg(float deg);
  static float angularDistanceDeg(float a, float b);

  ObstacleSnapshot obstacle_;
  LidarStats stats_;
  uint8_t buffer_[kPacketLength] = {0};
  uint8_t index_ = 0;
  bool have_prev_start_ = false;
  float prev_start_angle_ = 0.0f;

  // Per-rotation accumulators (mm). A "clear" sector keeps the max sentinel.
  int sector_front_left_ = 0;
  int sector_front_center_ = 0;
  int sector_front_right_ = 0;
  int sector_side_left_ = 0;
  int sector_side_right_ = 0;
};

}  // namespace followbox
