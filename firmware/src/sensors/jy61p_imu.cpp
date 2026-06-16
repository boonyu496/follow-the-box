#include "sensors/jy61p_imu.h"

#include "config/profile_defaults.h"
#include "core/time_utils.h"

namespace followbox {

void Jy61pImu::reset() {
  imu_ = ImuSnapshot{};
  stats_ = ImuStats{};
  index_ = 0;
  have_angle_ = false;
}

int16_t Jy61pImu::readInt16(const uint8_t* p) {
  // WitMotion payload is little-endian (low byte first).
  return static_cast<int16_t>(static_cast<uint16_t>(p[0]) |
                              (static_cast<uint16_t>(p[1]) << 8));
}

bool Jy61pImu::pushByte(uint8_t byte, uint32_t now_ms) {
  ++stats_.rx_byte_count;

  // Resync: the first byte must be the frame header.
  if (index_ == 0) {
    if (byte != kHeader) {
      return false;
    }
    buffer_[index_++] = byte;
    return false;
  }

  buffer_[index_++] = byte;
  if (index_ < kFrameLength) {
    return false;
  }

  index_ = 0;

  // Checksum = low byte of the sum of the first 10 bytes.
  uint8_t sum = 0;
  for (uint8_t i = 0; i < kFrameLength - 1; ++i) {
    sum = static_cast<uint8_t>(sum + buffer_[i]);
  }
  if (sum != buffer_[kFrameLength - 1]) {
    ++stats_.checksum_error_count;
    return false;
  }

  ++stats_.frame_count;
  const bool updated = (buffer_[1] == kTypeGyro || buffer_[1] == kTypeAngle);
  parseFrame(now_ms);
  return updated;
}

void Jy61pImu::parseFrame(uint32_t now_ms) {
  const uint8_t type = buffer_[1];
  const uint8_t* d = &buffer_[2];

  switch (type) {
    case kTypeGyro: {
      // wx, wy, wz, (temp). wz is the yaw rate; scale int16 to deg/s.
      const int16_t wz = readInt16(d + 4);
      imu_.yaw_rate_dps = profile::IMU_YAW_SIGN *
                          (static_cast<float>(wz) / 32768.0f) *
                          profile::IMU_GYRO_FULL_SCALE_DPS;
      ++stats_.gyro_frame_count;
      break;
    }
    case kTypeAngle: {
      // roll, pitch, yaw, (version). Scale int16 to degrees.
      const int16_t roll = readInt16(d + 0);
      const int16_t pitch = readInt16(d + 2);
      const int16_t yaw = readInt16(d + 4);
      const float scale = profile::IMU_ANGLE_FULL_SCALE_DEG / 32768.0f;
      imu_.roll_deg = static_cast<float>(roll) * scale;
      imu_.pitch_deg = static_cast<float>(pitch) * scale;
      imu_.yaw_deg = profile::IMU_YAW_SIGN * static_cast<float>(yaw) * scale;
      have_angle_ = true;
      ++stats_.angle_frame_count;
      break;
    }
    default:
      // Accel (0x51) and any other frame types are not used by the controller.
      return;
  }

  stats_.last_frame_ms = now_ms;
  imu_.last_update_ms = now_ms;
  // Only advertise valid once orientation is known; gyro alone is not enough
  // for the install-wizard yaw reference.
  imu_.valid = have_angle_;
}

void Jy61pImu::update(uint32_t now_ms) {
  if (imu_.valid && isStale(now_ms, imu_.last_update_ms,
                            profile::IMU_PARSER_TIMEOUT_MS)) {
    imu_.valid = false;
    imu_.yaw_rate_dps = 0.0f;
    have_angle_ = false;
  }
}

}  // namespace followbox
