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
  previous_raw_byte_ = 0;
  have_previous_raw_byte_ = false;
  aa55_preview_started_ = false;
  have_scan_points_ = false;
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
  if (byte == 0xAA) buffer_[index_++] = byte;
}

void LidarEaiS2::trackRawByte(uint8_t byte) {
  if (raw_preview_size_ < kDiagnosticPreviewSize) {
    raw_preview_[raw_preview_size_++] = byte;
  }

  const bool aa55 =
      have_previous_raw_byte_ && previous_raw_byte_ == 0xAA && byte == 0x55;
  const bool ld_header =
      have_previous_raw_byte_ && previous_raw_byte_ == 0x54 && byte == 0x2C;
  if (aa55) {
    ++stats_.aa55_header_count;
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

  previous_raw_byte_ = byte;
  have_previous_raw_byte_ = true;
}

bool LidarEaiS2::pushByte(uint8_t byte, uint32_t now_ms) {
  ++stats_.rx_byte_count;
  trackRawByte(byte);
  if (index_ == 0 && byte != 0xAA) return false;
  if (index_ == 1 && byte != 0x55) {
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
    expected_length_ =
        kHeaderSize + static_cast<size_t>(count) * kSampleSize;
  }
  if (index_ == 6 && (buffer_[4] & 0x01u) == 0) {
    ++stats_.framing_error_count;
    ++stats_.first_angle_reject_count;
    resync(byte);
    return false;
  }
  if (index_ == 8 && (buffer_[6] & 0x01u) == 0) {
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

  const size_t length = expected_length_;
  index_ = 0;
  expected_length_ = 0;
  if (!checksumValid(length)) {
    ++stats_.checksum_error_count;
    return false;
  }
  parsePacket(now_ms);
  return true;
}

bool LidarEaiS2::checksumValid(size_t length) const {
  if (length < kHeaderSize ||
      ((length - kHeaderSize) % kSampleSize) != 0) {
    return false;
  }

  uint16_t checksum = readLe16(buffer_);  // PH
  checksum ^= readLe16(buffer_ + 2);      // CT | LSN
  checksum ^= readLe16(buffer_ + 4);      // FSA
  checksum ^= readLe16(buffer_ + 6);      // LSA
  const uint8_t count = buffer_[3];
  for (uint8_t i = 0; i < count; ++i) {
    const size_t sample_offset =
        kHeaderSize + static_cast<size_t>(i) * kSampleSize;
    checksum ^= buffer_[sample_offset];  // 8-bit quality
    checksum ^= readLe16(buffer_ + sample_offset + 1);  // distance
  }
  return checksum == readLe16(buffer_ + 8);
}

void LidarEaiS2::parsePacket(uint32_t now_ms) {
  const bool ring_start = (buffer_[2] & 0x01u) != 0;
  if (ring_start && have_scan_points_) finalizeScan(now_ms);

  ++stats_.packet_count;
  stats_.last_packet_ms = now_ms;
  const uint8_t count = buffer_[3];
  const float first_angle = static_cast<float>(readLe16(buffer_ + 4) >> 1) / 64.0f;
  const float last_angle = static_cast<float>(readLe16(buffer_ + 6) >> 1) / 64.0f;
  float span = last_angle - first_angle;
  if (span < 0.0f) span += 360.0f;
  const float step = count > 1 ? span / static_cast<float>(count - 1) : 0.0f;

  for (uint8_t i = 0; i < count; ++i) {
    const size_t sample_offset =
        kHeaderSize + static_cast<size_t>(i) * kSampleSize;
    const uint16_t raw = readLe16(buffer_ + sample_offset + 1);
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
    angle = normalizeAngleDeg(angle - profile::LIDAR_MOUNT_YAW_OFFSET_DEG);
    if (angle > 180.0f) angle -= 360.0f;
    accumulatePoint(angle, distance_mm);
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
  resetSectors();
}

void LidarEaiS2::update(uint32_t now_ms) {
  if (obstacle_.valid &&
      elapsedMs(now_ms, obstacle_.last_update_ms) >
          profile::LIDAR_PACKET_TIMEOUT_MS) {
    obstacle_.valid = false;
    have_scan_points_ = false;
    resetSectors();
  }
}

}  // namespace followbox
