#pragma once

#include <cstddef>
#include <cstdint>

#include "core/types.h"

namespace followbox {

struct LidarS2Stats {
  uint32_t rx_byte_count = 0;
  uint32_t packet_count = 0;
  uint32_t checksum_error_count = 0;
  uint32_t framing_error_count = 0;
  uint32_t point_count = 0;
  uint32_t scan_count = 0;
  uint32_t last_packet_ms = 0;
};

// Streaming parser for the YDLIDAR/EAI S2 single-channel triangle protocol.
// Packet layout and formulas follow the vendor YDLidar-SDK communication
// protocol: AA 55, CT, LSN, FSA, LSA, XOR checksum, LSN x uint16 samples.
// The parser is pure logic: it never performs UART I/O or controls the motor.
class LidarEaiS2 {
 public:
  void reset();
  bool pushByte(uint8_t byte, uint32_t now_ms);
  void update(uint32_t now_ms);

  const ObstacleSnapshot& snapshot() const { return obstacle_; }
  const LidarS2Stats& stats() const { return stats_; }

 private:
  static constexpr size_t kHeaderSize = 10;
  static constexpr size_t kMaxPoints = 80;
  static constexpr size_t kMaxPacketSize = kHeaderSize + kMaxPoints * 2;

  static uint16_t readLe16(const uint8_t* p);
  static float normalizeAngleDeg(float deg);
  static float angularDistanceDeg(float a, float b);
  bool checksumValid(size_t length) const;
  void parsePacket(uint32_t now_ms);
  void accumulatePoint(float robot_angle_deg, uint16_t distance_mm);
  void finalizeScan(uint32_t now_ms);
  void resetSectors();
  void resync(uint8_t byte);

  ObstacleSnapshot obstacle_;
  LidarS2Stats stats_;
  uint8_t buffer_[kMaxPacketSize] = {};
  size_t index_ = 0;
  size_t expected_length_ = 0;
  bool have_scan_points_ = false;
  int sector_front_left_ = 0;
  int sector_front_center_ = 0;
  int sector_front_right_ = 0;
  int sector_side_left_ = 0;
  int sector_side_right_ = 0;
};

}  // namespace followbox
