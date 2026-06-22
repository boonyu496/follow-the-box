#pragma once

#include <cstddef>
#include <cstdint>

#include "core/types.h"

namespace followbox {

struct LidarS2Stats {
  uint32_t rx_byte_count = 0;
  uint32_t aa55_header_count = 0;
  uint32_t ld_header_count = 0;
  uint32_t packet_count = 0;
  uint32_t checksum_error_count = 0;
  uint32_t framing_error_count = 0;
  uint32_t invalid_count_reject_count = 0;
  uint32_t first_angle_reject_count = 0;
  uint32_t last_angle_reject_count = 0;
  uint32_t overflow_reject_count = 0;
  uint32_t point_count = 0;
  uint32_t scan_count = 0;
  uint32_t last_packet_ms = 0;
};

// Streaming parser for the fitted 150000-baud YDLIDAR/EAI single-channel
// module in 8-bit intensity mode. The vendor protocol is:
// AA 55, CT, LSN, FSA, LSA, CS, then
// LSN x (uint8 quality + uint16 distance).
// The parser is pure logic: it never performs UART I/O or controls the motor.
class LidarEaiS2 {
 public:
  void reset();
  bool pushByte(uint8_t byte, uint32_t now_ms);
  void update(uint32_t now_ms);

  const ObstacleSnapshot& snapshot() const { return obstacle_; }
  const LidarS2Stats& stats() const { return stats_; }
  const uint8_t* rawPreview() const { return raw_preview_; }
  size_t rawPreviewSize() const { return raw_preview_size_; }
  const uint8_t* aa55Preview() const { return aa55_preview_; }
  size_t aa55PreviewSize() const { return aa55_preview_size_; }

 private:
  static constexpr size_t kDiagnosticPreviewSize = 48;
  static constexpr size_t kHeaderSize = 10;
  static constexpr size_t kSampleSize = 3;
  static constexpr size_t kMaxPoints = 80;
  static constexpr size_t kMaxPacketSize =
      kHeaderSize + kMaxPoints * kSampleSize;

  static uint16_t readLe16(const uint8_t* p);
  static float normalizeAngleDeg(float deg);
  static float angularDistanceDeg(float a, float b);
  void trackRawByte(uint8_t byte);
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
  uint8_t raw_preview_[kDiagnosticPreviewSize] = {};
  size_t raw_preview_size_ = 0;
  uint8_t aa55_preview_[kDiagnosticPreviewSize] = {};
  size_t aa55_preview_size_ = 0;
  uint8_t previous_raw_byte_ = 0;
  bool have_previous_raw_byte_ = false;
  bool aa55_preview_started_ = false;
  bool have_scan_points_ = false;
  int sector_front_left_ = 0;
  int sector_front_center_ = 0;
  int sector_front_right_ = 0;
  int sector_side_left_ = 0;
  int sector_side_right_ = 0;
};

}  // namespace followbox
