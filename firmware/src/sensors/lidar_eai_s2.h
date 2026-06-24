#pragma once

#include <cstddef>
#include <cstdint>

#include "core/types.h"

namespace followbox {

struct LidarS2Stats {
  uint32_t rx_byte_count = 0;
  uint32_t aa55_header_count = 0;
  uint32_t header_55aa_count = 0;
  uint32_t ld_header_count = 0;
  uint32_t packet_count = 0;
  uint32_t header_55aa_packet_count = 0;
  uint32_t no_checksum_packet_count = 0;
  uint32_t no_intensity_packet_count = 0;  // NODE_QUAL0: 2-byte/sample, no quality
  uint32_t checksum_error_count = 0;
  uint32_t framing_error_count = 0;
  uint32_t invalid_count_reject_count = 0;
  uint32_t first_angle_reject_count = 0;
  uint32_t last_angle_reject_count = 0;
  uint32_t overflow_reject_count = 0;
  uint32_t point_count = 0;
  uint32_t scan_count = 0;
  uint32_t last_rx_ms = 0;
  uint32_t last_packet_ms = 0;
};

// Streaming parser for the fitted YDLIDAR/EAI S2 single-channel module in
// 8-bit intensity mode. The vendor/EaiLidarTest stream carries bytes:
// AA 55, CT, LSN, FSA, LSA, CS, then LSN x (quality + uint16 distance).
// Buyer ROS drivers omit CS and place the 2-byte distance before intensity;
// that variant is accepted only when the next AA55 header is already present.
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
  const uint8_t* header55aaPreview() const { return header_55aa_preview_; }
  size_t header55aaPreviewSize() const { return header_55aa_preview_size_; }

 private:
  enum class PacketLayout : uint8_t {
    kChecksumQualityFirst,      // CS present, 3-byte samples (quality+dist)
    kNoChecksumDistanceFirst,   // No CS, 3-byte samples (dist+quality, ROS fmt)
    kChecksumDistanceOnly,      // CS present, 2-byte samples (dist only, NODE_QUAL0)
    kHeader55aaDistanceFirst,   // Field-captured 55 AA 03 LSN frames, dist+quality
  };

  static constexpr size_t kDiagnosticPreviewSize = 48;
  static constexpr size_t kHeaderSizeNoChecksum = 8;
  static constexpr size_t kHeaderSizeWithChecksum = 10;
  static constexpr size_t kSampleSize = 3;           // NODE_QUAL8: quality+dist
  static constexpr size_t kSampleSizeNoIntensity = 2; // NODE_QUAL0: dist only
  static constexpr size_t kMaxPoints = 80;
  static constexpr size_t kMaxPacketSize =
      kHeaderSizeWithChecksum + kMaxPoints * kSampleSize;

  static uint16_t readLe16(const uint8_t* p);
  static float normalizeAngleDeg(float deg);
  static float angularDistanceDeg(float a, float b);
  bool headerIs55aa() const;
  void trackRawByte(uint8_t byte);
  bool header55aaPacketPlausible(size_t length) const;
  bool checksumValid(size_t length) const;
  bool checksumValidNoIntensity(size_t length) const;
  void parsePacket(uint32_t now_ms, PacketLayout layout);
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
  uint8_t header_55aa_preview_[kDiagnosticPreviewSize] = {};
  size_t header_55aa_preview_size_ = 0;
  uint8_t previous_raw_byte_ = 0;
  bool have_previous_raw_byte_ = false;
  bool aa55_preview_started_ = false;
  bool header_55aa_preview_started_ = false;
  bool have_scan_points_ = false;
  float last_scan_angle_deg_ = -1.0f;
  int sector_front_left_ = 0;
  int sector_front_center_ = 0;
  int sector_front_right_ = 0;
  int sector_side_left_ = 0;
  int sector_side_right_ = 0;
};

}  // namespace followbox
