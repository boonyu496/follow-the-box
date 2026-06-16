#include "sensors/lidar_ld19.h"

#include <algorithm>
#include <cmath>

#include "config/profile_defaults.h"
#include "core/time_utils.h"

namespace followbox {
namespace {

// Official LDROBOT LD06/LD19 CRC-8 lookup table.
const uint8_t kCrcTable[256] = {
    0x00, 0x4d, 0x9a, 0xd7, 0x79, 0x34, 0xe3, 0xae, 0xf2, 0xbf, 0x68, 0x25, 0x8b,
    0xc6, 0x11, 0x5c, 0xa9, 0xe4, 0x33, 0x7e, 0xd0, 0x9d, 0x4a, 0x07, 0x5b, 0x16,
    0xc1, 0x8c, 0x22, 0x6f, 0xb8, 0xf5, 0x1f, 0x52, 0x85, 0xc8, 0x66, 0x2b, 0xfc,
    0xb1, 0xed, 0xa0, 0x77, 0x3a, 0x94, 0xd9, 0x0e, 0x43, 0xb6, 0xfb, 0x2c, 0x61,
    0xcf, 0x82, 0x55, 0x18, 0x44, 0x09, 0xde, 0x93, 0x3d, 0x70, 0xa7, 0xea, 0x3e,
    0x73, 0xa4, 0xe9, 0x47, 0x0a, 0xdd, 0x90, 0xcc, 0x81, 0x56, 0x1b, 0xb5, 0xf8,
    0x2f, 0x62, 0x97, 0xda, 0x0d, 0x40, 0xee, 0xa3, 0x74, 0x39, 0x65, 0x28, 0xff,
    0xb2, 0x1c, 0x51, 0x86, 0xcb, 0x21, 0x6c, 0xbb, 0xf6, 0x58, 0x15, 0xc2, 0x8f,
    0xd3, 0x9e, 0x49, 0x04, 0xaa, 0xe7, 0x30, 0x7d, 0x88, 0xc5, 0x12, 0x5f, 0xf1,
    0xbc, 0x6b, 0x26, 0x7a, 0x37, 0xe0, 0xad, 0x03, 0x4e, 0x99, 0xd4, 0x7c, 0x31,
    0xe6, 0xab, 0x05, 0x48, 0x9f, 0xd2, 0x8e, 0xc3, 0x14, 0x59, 0xf7, 0xba, 0x6d,
    0x20, 0xd5, 0x98, 0x4f, 0x02, 0xac, 0xe1, 0x36, 0x7b, 0x27, 0x6a, 0xbd, 0xf0,
    0x5e, 0x13, 0xc4, 0x89, 0x63, 0x2e, 0xf9, 0xb4, 0x1a, 0x57, 0x80, 0xcd, 0x91,
    0xdc, 0x0b, 0x46, 0xe8, 0xa5, 0x72, 0x3f, 0xca, 0x87, 0x50, 0x1d, 0xb3, 0xfe,
    0x29, 0x64, 0x38, 0x75, 0xa2, 0xef, 0x41, 0x0c, 0xdb, 0x96, 0x42, 0x0f, 0xd8,
    0x95, 0x3b, 0x76, 0xa1, 0xec, 0xb0, 0xfd, 0x2a, 0x67, 0xc9, 0x84, 0x53, 0x1e,
    0xeb, 0xa6, 0x71, 0x3c, 0x92, 0xdf, 0x08, 0x45, 0x19, 0x54, 0x83, 0xce, 0x60,
    0x2d, 0xfa, 0xb7, 0x5d, 0x10, 0xc7, 0x8a, 0x24, 0x69, 0xbe, 0xf3, 0xaf, 0xe2,
    0x35, 0x78, 0xd6, 0x9b, 0x4c, 0x01, 0xa8, 0xe5, 0x32, 0x7f, 0xd1, 0x9c, 0x4b,
    0x06, 0x5a, 0x17, 0xc0, 0x8d, 0x23, 0x6e, 0xb9, 0xf4};

}  // namespace

void LidarLd19::reset() {
  obstacle_ = ObstacleSnapshot{};
  stats_ = LidarStats{};
  index_ = 0;
  have_prev_start_ = false;
  prev_start_angle_ = 0.0f;
  resetSectors();
}

void LidarLd19::resetSectors() {
  const int clear = profile::LIDAR_MAX_VALID_MM;
  sector_front_left_ = clear;
  sector_front_center_ = clear;
  sector_front_right_ = clear;
  sector_side_left_ = clear;
  sector_side_right_ = clear;
}

uint8_t LidarLd19::crc8(const uint8_t* data, uint8_t length) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < length; ++i) {
    crc = kCrcTable[(crc ^ data[i]) & 0xff];
  }
  return crc;
}

float LidarLd19::normalizeAngleDeg(float deg) {
  while (deg >= 360.0f) {
    deg -= 360.0f;
  }
  while (deg < 0.0f) {
    deg += 360.0f;
  }
  return deg;
}

float LidarLd19::angularDistanceDeg(float a, float b) {
  float diff = std::fabs(a - b);
  if (diff > 180.0f) {
    diff = 360.0f - diff;
  }
  return diff;
}

bool LidarLd19::pushByte(uint8_t byte, uint32_t now_ms) {
  ++stats_.rx_byte_count;

  if (index_ == 0) {
    if (byte != kHeader) {
      return false;
    }
  } else if (index_ == 1 && byte != kVerLen) {
    // Bad length/version: drop and resync (the byte may itself be a header).
    index_ = 0;
    if (byte == kHeader) {
      buffer_[index_++] = byte;
    }
    return false;
  }

  buffer_[index_++] = byte;
  if (index_ < kPacketLength) {
    return false;
  }

  index_ = 0;
  if (crc8(buffer_, kPacketLength - 1) != buffer_[kPacketLength - 1]) {
    ++stats_.crc_error_count;
    return false;
  }

  parsePacket(now_ms);
  return true;
}

void LidarLd19::parsePacket(uint32_t now_ms) {
  ++stats_.packet_count;
  stats_.last_packet_ms = now_ms;
  stats_.last_speed_dps =
      static_cast<float>((buffer_[3] << 8) | buffer_[2]);

  const float start_angle =
      static_cast<float>((buffer_[5] << 8) | buffer_[4]) * 0.01f;
  const float end_angle =
      static_cast<float>((buffer_[43] << 8) | buffer_[42]) * 0.01f;

  // A new rotation begins when the start angle wraps past 360 -> 0.
  if (have_prev_start_ && start_angle < prev_start_angle_) {
    finalizeScan(now_ms);
  }
  have_prev_start_ = true;
  prev_start_angle_ = start_angle;

  float span = end_angle - start_angle;
  if (span < 0.0f) {
    span += 360.0f;
  }
  const float step = span / static_cast<float>(kPointCount - 1);

  for (uint8_t i = 0; i < kPointCount; ++i) {
    const uint8_t base = 6 + i * 3;
    const uint16_t distance_mm =
        static_cast<uint16_t>((buffer_[base + 1] << 8) | buffer_[base]);
    const uint8_t intensity = buffer_[base + 2];
    ++stats_.point_count;

    if (distance_mm < profile::LIDAR_MIN_VALID_MM ||
        distance_mm > profile::LIDAR_MAX_VALID_MM ||
        intensity < profile::LIDAR_MIN_INTENSITY) {
      continue;
    }

    const float lidar_angle = normalizeAngleDeg(start_angle + step * i);
    // Map lidar frame to robot frame; positive = left of forward.
    // Sign/offset require bench verification (see header).
    float robot_angle = lidar_angle - profile::LIDAR_MOUNT_YAW_OFFSET_DEG;
    robot_angle = normalizeAngleDeg(robot_angle);
    if (robot_angle > 180.0f) {
      robot_angle -= 360.0f;
    }
    accumulatePoint(robot_angle, distance_mm, intensity);
  }
}

void LidarLd19::accumulatePoint(float robot_angle_deg, uint16_t distance_mm,
                                uint8_t /*intensity*/) {
  const float mag = std::fabs(robot_angle_deg);
  const int dist = static_cast<int>(distance_mm);

  if (mag <= profile::LIDAR_FRONT_CENTER_HALF_DEG) {
    sector_front_center_ = std::min(sector_front_center_, dist);
  } else if (mag <= profile::LIDAR_FRONT_SIDE_HALF_DEG) {
    if (robot_angle_deg > 0.0f) {
      sector_front_left_ = std::min(sector_front_left_, dist);
    } else {
      sector_front_right_ = std::min(sector_front_right_, dist);
    }
  }

  if (angularDistanceDeg(mag, profile::LIDAR_SIDE_CENTER_DEG) <=
      profile::LIDAR_SIDE_HALF_DEG) {
    if (robot_angle_deg > 0.0f) {
      sector_side_left_ = std::min(sector_side_left_, dist);
    } else {
      sector_side_right_ = std::min(sector_side_right_, dist);
    }
  }
}

void LidarLd19::finalizeScan(uint32_t now_ms) {
  obstacle_.valid = true;
  obstacle_.last_update_ms = now_ms;
  obstacle_.front_left_mm = sector_front_left_;
  obstacle_.front_center_mm = sector_front_center_;
  obstacle_.front_right_mm = sector_front_right_;
  obstacle_.side_left_mm = sector_side_left_;
  obstacle_.side_right_mm = sector_side_right_;
  ++stats_.scan_count;
  resetSectors();
}

void LidarLd19::update(uint32_t now_ms) {
  if (!obstacle_.valid) {
    return;
  }
  if (elapsedMs(now_ms, obstacle_.last_update_ms) >
      profile::LIDAR_PACKET_TIMEOUT_MS) {
    obstacle_.valid = false;
    have_prev_start_ = false;
    resetSectors();
  }
}

}  // namespace followbox
