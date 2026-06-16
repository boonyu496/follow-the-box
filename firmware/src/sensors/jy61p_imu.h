#pragma once

#include <cstdint>

#include "core/types.h"

namespace followbox {

// Bring-up diagnostics for the JY61P stream. Exposed to telemetry only.
struct ImuStats {
  uint32_t rx_byte_count = 0;
  uint32_t frame_count = 0;
  uint32_t checksum_error_count = 0;
  uint32_t angle_frame_count = 0;
  uint32_t gyro_frame_count = 0;
  uint32_t last_frame_ms = 0;
};

// Pure-logic parser for the JY61P / WitMotion IMU serial stream (JY61P.md).
//
// Frame (11 bytes, little-endian int16 payload):
//   [0]    header  0x55
//   [1]    type    0x51 accel | 0x52 gyro | 0x53 angle (other types ignored)
//   [2..9] four int16 fields
//   [10]   checksum = sum(bytes[0..9]) & 0xFF
//
// yaw_rate_dps comes from the 0x52 gyro frame (wz); yaw/pitch/roll from the
// 0x53 angle frame. RX-only: this never configures the module and never touches
// GPIO/UART - callers feed raw bytes via pushByte(). yaw sign and the real baud
// MUST be confirmed in the install wizard before yaw damping is trusted.
class Jy61pImu {
 public:
  void reset();

  // Feed one received byte. Returns true when a full, checksum-valid frame
  // updated the snapshot.
  bool pushByte(uint8_t byte, uint32_t now_ms);

  // Invalidate the snapshot when the stream stops (staleness timeout).
  void update(uint32_t now_ms);

  const ImuSnapshot& snapshot() const { return imu_; }
  const ImuStats& stats() const { return stats_; }

 private:
  static constexpr uint8_t kHeader = 0x55;
  static constexpr uint8_t kFrameLength = 11;
  static constexpr uint8_t kTypeAccel = 0x51;
  static constexpr uint8_t kTypeGyro = 0x52;
  static constexpr uint8_t kTypeAngle = 0x53;

  void parseFrame(uint32_t now_ms);
  static int16_t readInt16(const uint8_t* p);

  ImuSnapshot imu_;
  ImuStats stats_;
  uint8_t buffer_[kFrameLength] = {0};
  uint8_t index_ = 0;
  // Mark valid only once an angle frame has been seen (yaw is the primary use).
  bool have_angle_ = false;
};

}  // namespace followbox
