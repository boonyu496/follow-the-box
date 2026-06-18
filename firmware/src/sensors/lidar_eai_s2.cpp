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

bool LidarEaiS2::pushByte(uint8_t byte, uint32_t now_ms) {
  ++stats_.rx_byte_count;
  if (index_ == 0 && byte != 0xAA) return false;
  if (index_ == 1 && byte != 0x55) {
    ++stats_.framing_error_count;
    resync(byte);
    return false;
  }

  if (index_ >= kMaxPacketSize) {
    ++stats_.framing_error_count;
    resync(byte);
    return false;
  }
  buffer_[index_++] = byte;

  if (index_ == 4) {
    const uint8_t count = buffer_[3];
    if (count == 0 || count > kMaxPoints) {
      ++stats_.framing_error_count;
      resync(byte);
      return false;
    }
    expected_length_ = kHeaderSize + static_cast<size_t>(count) * 2;
  }
  if ((index_ == 6 && (buffer_[4] & 0x01u) == 0) ||
      (index_ == 8 && (buffer_[6] & 0x01u) == 0)) {
    ++stats_.framing_error_count;
    resync(byte);
    return false;
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
  if (length < kHeaderSize || ((length - kHeaderSize) % 2) != 0) return false;
  uint16_t checksum = readLe16(buffer_);       // PH
  checksum ^= readLe16(buffer_ + 2);           // CT | LSN
  checksum ^= readLe16(buffer_ + 4);           // FSA
  checksum ^= readLe16(buffer_ + 6);           // LSA
  for (size_t offset = kHeaderSize; offset < length; offset += 2) {
    checksum ^= readLe16(buffer_ + offset);
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
    const uint16_t raw = readLe16(buffer_ + kHeaderSize + i * 2);
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
