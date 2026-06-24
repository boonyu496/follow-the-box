#include "sensors/lidar_eai_s2.h"

#include <algorithm>
#include <cmath>

#include "config/profile_defaults.h"
#include "core/time_utils.h"

namespace followbox {

void LidarEaiS2::reset() {
  obstacle_ = ObstacleSnapshot{};
  stats_ = LidarS2Stats{};
  index_ = 0;
  expected_length_ = 0;
  std::fill(raw_preview_, raw_preview_ + kDiagnosticPreviewSize, 0);
  raw_preview_size_ = 0;
  std::fill(aa55_preview_, aa55_preview_ + kDiagnosticPreviewSize, 0);
  aa55_preview_size_ = 0;
  std::fill(header_55aa_preview_, header_55aa_preview_ + kDiagnosticPreviewSize, 0);
  header_55aa_preview_size_ = 0;
  previous_raw_byte_ = 0;
  have_previous_raw_byte_ = false;
  aa55_preview_started_ = false;
  header_55aa_preview_started_ = false;
  have_scan_points_ = false;
  last_scan_angle_deg_ = -1.0f;
  resetSectors();
}

uint16_t LidarEaiS2::readLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
}

float LidarEaiS2::normalizeAngleDeg(float deg) {
  while (deg >= 360.0f) deg -= 360.0f;
  while (deg < 0.0f) deg += 360.0f;
  return deg;
}

float LidarEaiS2::angularDistanceDeg(float a, float b) {
  float diff = std::fabs(a - b);
  return diff > 180.0f ? 360.0f - diff : diff;
}

void LidarEaiS2::resetSectors() {
  sector_front_left_ = 0;
  sector_front_center_ = 0;
  sector_front_right_ = 0;
  sector_side_left_ = 0;
  sector_side_right_ = 0;
}

void LidarEaiS2::resync(uint8_t byte) {
  index_ = 0;
  expected_length_ = 0;
  if (byte == 0xAA || byte == 0x55) buffer_[index_++] = byte;
}

bool LidarEaiS2::headerIs55aa() const {
  return index_ >= 2 && buffer_[0] == 0x55 && buffer_[1] == 0xAA;
}

void LidarEaiS2::trackRawByte(uint8_t byte) {
  if (raw_preview_size_ < kDiagnosticPreviewSize) {
    raw_preview_[raw_preview_size_++] = byte;
  }

  const bool aa55 =
      have_previous_raw_byte_ && previous_raw_byte_ == 0xAA && byte == 0x55;
  const bool header_55aa =
      have_previous_raw_byte_ && previous_raw_byte_ == 0x55 && byte == 0xAA;
  const bool ld_header =
      have_previous_raw_byte_ && previous_raw_byte_ == 0x54 && byte == 0x2C;
  if (aa55) {
    ++stats_.aa55_header_count;
  }
  if (header_55aa) {
    ++stats_.header_55aa_count;
  }
  if (ld_header) {
    ++stats_.ld_header_count;
  }

  if (!aa55_preview_started_ && aa55) {
    aa55_preview_[0] = 0xAA;
    aa55_preview_[1] = 0x55;
    aa55_preview_size_ = 2;
    aa55_preview_started_ = true;
  } else if (aa55_preview_started_ &&
             aa55_preview_size_ < kDiagnosticPreviewSize) {
    aa55_preview_[aa55_preview_size_++] = byte;
  }

  if (!header_55aa_preview_started_ && header_55aa) {
    header_55aa_preview_[0] = 0x55;
    header_55aa_preview_[1] = 0xAA;
    header_55aa_preview_size_ = 2;
    header_55aa_preview_started_ = true;
  } else if (header_55aa_preview_started_ &&
             header_55aa_preview_size_ < kDiagnosticPreviewSize) {
    header_55aa_preview_[header_55aa_preview_size_++] = byte;
  }

  previous_raw_byte_ = byte;
  have_previous_raw_byte_ = true;
}

bool LidarEaiS2::pushByte(uint8_t byte, uint32_t now_ms) {
  ++stats_.rx_byte_count;
  stats_.last_rx_ms = now_ms;
  trackRawByte(byte);
  if (index_ == 0 && byte != 0xAA && byte != 0x55) return false;
  if (index_ == 1) {
    const bool aa55 = buffer_[0] == 0xAA && byte == 0x55;
    const bool header_55aa = buffer_[0] == 0x55 && byte == 0xAA;
    if (aa55 || header_55aa) {
      buffer_[index_++] = byte;
      return false;
    }
    ++stats_.framing_error_count;
    resync(byte);
    return false;
  }

  if (index_ >= kMaxPacketSize) {
    ++stats_.framing_error_count;
    ++stats_.overflow_reject_count;
    resync(byte);
    return false;
  }
  buffer_[index_++] = byte;

  if (index_ == 4) {
    const uint8_t count = buffer_[3];
    if (count == 0 || count > kMaxPoints) {
      ++stats_.framing_error_count;
      ++stats_.invalid_count_reject_count;
      resync(byte);
      return false;
    }
    if (headerIs55aa()) {
      expected_length_ = kHeaderSizeNoChecksum +
                         static_cast<size_t>(count) * kSampleSize;
    } else {
      // Start with the shorter NODE_QUAL0 target (2 bytes/sample, no intensity).
      // If that checksum fails we extend to the NODE_QUAL8 target (3 bytes/sample).
      expected_length_ =
          kHeaderSizeWithChecksum + static_cast<size_t>(count) * kSampleSizeNoIntensity;
    }
  }
  if (!headerIs55aa() && index_ == 6 && (buffer_[4] & 0x01u) == 0) {
    ++stats_.framing_error_count;
    ++stats_.first_angle_reject_count;
    resync(byte);
    return false;
  }
  if (!headerIs55aa() && index_ == 8 && (buffer_[6] & 0x01u) == 0) {
    ++stats_.framing_error_count;
    ++stats_.last_angle_reject_count;
    resync(byte);
    return false;
  }
  if (index_ == 8) {
    constexpr uint16_t kMaxEncodedAngle = 360u * 64u;
    const uint16_t first_angle = readLe16(buffer_ + 4) >> 1;
    const uint16_t last_angle = readLe16(buffer_ + 6) >> 1;
    if (first_angle >= kMaxEncodedAngle || last_angle >= kMaxEncodedAngle) {
      ++stats_.framing_error_count;
      resync(byte);
      return false;
    }
  }
  if (expected_length_ == 0 || index_ < expected_length_) return false;

  if (headerIs55aa()) {
    const size_t length = expected_length_;
    if (header55aaPacketPlausible(length)) {
      index_ = 0;
      expected_length_ = 0;
      parsePacket(now_ms, PacketLayout::kHeader55aaDistanceFirst);
      return true;
    }
    ++stats_.framing_error_count;
    const uint8_t last_byte = buffer_[length - 1];
    resync(last_byte);
    return false;
  }

  const uint8_t count = buffer_[3];
  const size_t len2 =
      kHeaderSizeWithChecksum + static_cast<size_t>(count) * kSampleSizeNoIntensity;
  const size_t len3 =
      kHeaderSizeWithChecksum + static_cast<size_t>(count) * kSampleSize;

  // --- Try NODE_QUAL0 (2-byte/sample, no intensity) first ---
  if (index_ == len2 && checksumValidNoIntensity(len2)) {
    index_ = 0;
    expected_length_ = 0;
    parsePacket(now_ms, PacketLayout::kChecksumDistanceOnly);
    return true;
  }

  // 2-byte checksum failed; extend target to NODE_QUAL8 (3-byte/sample)
  if (index_ < len3) {
    expected_length_ = len3;
    return false;
  }

  // --- Try NODE_QUAL8 (3-byte/sample, quality + distance) ---
  const size_t length_with_checksum = len3;
  const size_t length_without_checksum =
      kHeaderSizeNoChecksum + static_cast<size_t>(count) * kSampleSize;
  const bool trailing_next_header =
      length_with_checksum >= length_without_checksum + 2 &&
      buffer_[length_without_checksum] == 0xAA &&
      buffer_[length_without_checksum + 1] == 0x55;

  if (checksumValid(length_with_checksum)) {
    index_ = 0;
    expected_length_ = 0;
    parsePacket(now_ms, PacketLayout::kChecksumQualityFirst);
    return true;
  }

  if (trailing_next_header) {
    parsePacket(now_ms, PacketLayout::kNoChecksumDistanceFirst);
    buffer_[0] = 0xAA;
    buffer_[1] = 0x55;
    index_ = 2;
    expected_length_ = 0;
    return true;
  }

  ++stats_.checksum_error_count;
  const uint8_t last_byte = buffer_[length_with_checksum - 1];
  resync(last_byte);
  return false;
}

bool LidarEaiS2::header55aaPacketPlausible(size_t length) const {
  if (length < kHeaderSizeNoChecksum ||
      ((length - kHeaderSizeNoChecksum) % kSampleSize) != 0 ||
      buffer_[0] != 0x55 || buffer_[1] != 0xAA || buffer_[2] != 0x03) {
    return false;
  }
  const uint8_t count = buffer_[3];
  if (count == 0 || count > 32 ||
      length != kHeaderSizeNoChecksum + static_cast<size_t>(count) * kSampleSize) {
    return false;
  }

  constexpr uint16_t kMaxEncodedAngle = 360u * 64u;
  const uint16_t first_angle = readLe16(buffer_ + 4) >> 1;
  const uint16_t last_angle = readLe16(buffer_ + 6) >> 1;
  if (first_angle >= kMaxEncodedAngle || last_angle >= kMaxEncodedAngle) {
    return false;
  }

  uint8_t plausible_samples = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const size_t sample_offset =
        kHeaderSizeNoChecksum + static_cast<size_t>(i) * kSampleSize;
    const uint16_t raw = readLe16(buffer_ + sample_offset);
    const uint16_t mm_quarter = static_cast<uint16_t>(raw / 4u);
    const bool plausible_scaled =
        mm_quarter >= profile::LIDAR_MIN_VALID_MM &&
        mm_quarter <= profile::LIDAR_MAX_VALID_MM;
    const bool plausible_direct =
        raw >= profile::LIDAR_MIN_VALID_MM && raw <= profile::LIDAR_MAX_VALID_MM;
    if (plausible_scaled || plausible_direct) {
      ++plausible_samples;
    }
  }
  return plausible_samples > 0;
}

bool LidarEaiS2::checksumValidNoIntensity(size_t length) const {
  if (length < kHeaderSizeWithChecksum ||
      ((length - kHeaderSizeWithChecksum) % kSampleSizeNoIntensity) != 0) {
    return false;
  }
  uint16_t checksum = readLe16(buffer_);  // PH
  checksum ^= readLe16(buffer_ + 2);      // CT | LSN
  checksum ^= readLe16(buffer_ + 4);      // FSA
  checksum ^= readLe16(buffer_ + 6);      // LSA
  const uint8_t count = buffer_[3];
  for (uint8_t i = 0; i < count; ++i) {
    // NODE_QUAL0: only 2-byte distance word per sample, no quality byte
    checksum ^= readLe16(buffer_ + kHeaderSizeWithChecksum +
                         static_cast<size_t>(i) * kSampleSizeNoIntensity);
  }
  return checksum == readLe16(buffer_ + 8);
}

bool LidarEaiS2::checksumValid(size_t length) const {
  if (length < kHeaderSizeWithChecksum ||
      ((length - kHeaderSizeWithChecksum) % kSampleSize) != 0) {
    return false;
  }

  uint16_t checksum = readLe16(buffer_);  // PH
  checksum ^= readLe16(buffer_ + 2);      // CT | LSN
  checksum ^= readLe16(buffer_ + 4);      // FSA
  checksum ^= readLe16(buffer_ + 6);      // LSA
  const uint8_t count = buffer_[3];
  for (uint8_t i = 0; i < count; ++i) {
    const size_t sample_offset =
        kHeaderSizeWithChecksum + static_cast<size_t>(i) * kSampleSize;
    checksum ^= buffer_[sample_offset];  // 8-bit quality
    checksum ^= readLe16(buffer_ + sample_offset + 1);  // distance
  }
  return checksum == readLe16(buffer_ + 8);
}

void LidarEaiS2::parsePacket(uint32_t now_ms, PacketLayout layout) {
  const bool ring_start = (buffer_[2] & 0x01u) != 0;
  if (ring_start && have_scan_points_) finalizeScan(now_ms);

  ++stats_.packet_count;
  if (layout == PacketLayout::kNoChecksumDistanceFirst) {
    ++stats_.no_checksum_packet_count;
  } else if (layout == PacketLayout::kChecksumDistanceOnly) {
    ++stats_.no_intensity_packet_count;
  } else if (layout == PacketLayout::kHeader55aaDistanceFirst) {
    ++stats_.header_55aa_packet_count;
    ++stats_.no_checksum_packet_count;
  }
  stats_.last_packet_ms = now_ms;
  const uint8_t count = buffer_[3];
  const float first_angle = static_cast<float>(readLe16(buffer_ + 4) >> 1) / 64.0f;
  const float last_angle = static_cast<float>(readLe16(buffer_ + 6) >> 1) / 64.0f;
  float span = last_angle - first_angle;
  if (span < 0.0f) span += 360.0f;
  const float step = count > 1 ? span / static_cast<float>(count - 1) : 0.0f;

  const size_t sample_base = layout == PacketLayout::kNoChecksumDistanceFirst
                                 ? kHeaderSizeNoChecksum
                                 : layout == PacketLayout::kHeader55aaDistanceFirst
                                       ? kHeaderSizeNoChecksum
                                       : kHeaderSizeWithChecksum;
  const size_t sample_stride = layout == PacketLayout::kChecksumDistanceOnly
                                    ? kSampleSizeNoIntensity
                                    : kSampleSize;

  for (uint8_t i = 0; i < count; ++i) {
    const size_t sample_offset =
        sample_base + static_cast<size_t>(i) * sample_stride;
    // kChecksumQualityFirst: quality byte first, then 2-byte distance.
    // kNoChecksumDistanceFirst/kHeader55aaDistanceFirst: distance first, then quality.
    // kChecksumDistanceOnly: 2-byte distance only (no quality byte)
    const uint16_t raw =
        layout == PacketLayout::kChecksumQualityFirst
            ? readLe16(buffer_ + sample_offset + 1)
            : readLe16(buffer_ + sample_offset);
    const uint16_t distance_mm = static_cast<uint16_t>(raw / 4u);
    ++stats_.point_count;
    if (distance_mm < profile::LIDAR_MIN_VALID_MM ||
        distance_mm > profile::LIDAR_MAX_VALID_MM) {
      continue;
    }

    float angle = first_angle + step * static_cast<float>(i);
    const float correction = std::atan(
        21.8f * (155.3f - static_cast<float>(distance_mm)) /
        (155.3f * static_cast<float>(distance_mm)));
    angle += correction * 180.0f / 3.14159265358979323846f;
    angle = normalizeAngleDeg(angle);
    if (have_scan_points_ && last_scan_angle_deg_ >= 0.0f &&
        angle < last_scan_angle_deg_ - 180.0f) {
      finalizeScan(now_ms);
    }
    last_scan_angle_deg_ = angle;

    float robot_angle = normalizeAngleDeg(angle - profile::LIDAR_MOUNT_YAW_OFFSET_DEG);
    if (robot_angle > 180.0f) robot_angle -= 360.0f;
    accumulatePoint(robot_angle, distance_mm);
    have_scan_points_ = true;
  }
}

void LidarEaiS2::accumulatePoint(float robot_angle_deg, uint16_t distance_mm) {
  const float mag = std::fabs(robot_angle_deg);
  const int distance = static_cast<int>(distance_mm);
  auto take_nearest = [distance](int& sector) {
    sector = sector > 0 ? std::min(sector, distance) : distance;
  };

  if (mag <= profile::LIDAR_FRONT_CENTER_HALF_DEG) {
    take_nearest(sector_front_center_);
  } else if (mag <= profile::LIDAR_FRONT_SIDE_HALF_DEG) {
    take_nearest(robot_angle_deg > 0.0f ? sector_front_left_
                                        : sector_front_right_);
  }
  if (angularDistanceDeg(mag, profile::LIDAR_SIDE_CENTER_DEG) <=
      profile::LIDAR_SIDE_HALF_DEG) {
    take_nearest(robot_angle_deg > 0.0f ? sector_side_left_
                                        : sector_side_right_);
  }
}

void LidarEaiS2::finalizeScan(uint32_t now_ms) {
  obstacle_.valid = true;
  obstacle_.last_update_ms = now_ms;
  obstacle_.front_left_mm = sector_front_left_;
  obstacle_.front_center_mm = sector_front_center_;
  obstacle_.front_right_mm = sector_front_right_;
  obstacle_.side_left_mm = sector_side_left_;
  obstacle_.side_right_mm = sector_side_right_;
  ++stats_.scan_count;
  have_scan_points_ = false;
  last_scan_angle_deg_ = -1.0f;
  resetSectors();
}

void LidarEaiS2::update(uint32_t now_ms) {
  if (obstacle_.valid &&
      elapsedMs(now_ms, obstacle_.last_update_ms) >
          profile::LIDAR_PACKET_TIMEOUT_MS) {
    obstacle_.valid = false;
    have_scan_points_ = false;
    last_scan_angle_deg_ = -1.0f;
    resetSectors();
  }
}

}  // namespace followbox
