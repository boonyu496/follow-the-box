#include "sensors/uwb_gc_p2304.h"

#include <algorithm>
#include <cmath>

#include "config/profile_defaults.h"
#include "core/time_utils.h"

namespace followbox {
namespace {

constexpr uint8_t kFrameHeader = 0xF0;
constexpr uint8_t kFrameType = 0x06;
constexpr uint8_t kFrameTail = 0xAA;
constexpr uint8_t kFrameLength = 10;

}  // namespace

void UwbGcP2304Parser::reset() {
  target_ = UwbTarget{};
  stats_ = UwbParserStats{};
  index_ = 0;
  filter_init_ = false;
  ema_distance_cm_ = 0.0f;
  ema_bearing_deg_ = 0.0f;
}

bool UwbGcP2304Parser::pushByte(uint8_t byte, uint32_t now_ms) {
  ++stats_.rx_byte_count;

  // Resynchronise: a frame can only start at the header byte.
  if (index_ == 0 && byte != kFrameHeader) {
    return false;
  }

  buffer_[index_++] = byte;
  if (index_ < kFrameLength) {
    return false;
  }

  index_ = 0;
  if (buffer_[1] != kFrameType || buffer_[9] != kFrameTail) {
    ++stats_.parse_error_count;
    return false;
  }

  const uint16_t distance_cm =
      static_cast<uint16_t>((buffer_[5] << 8) | buffer_[4]);
  const int16_t angle_raw =
      static_cast<int16_t>((buffer_[7] << 8) | buffer_[6]);
  const int rssi_dbm = static_cast<int>(buffer_[8]) - 256;

  if (distance_cm < profile::UWB_MIN_VALID_DISTANCE_CM ||
      distance_cm > profile::UWB_MAX_VALID_DISTANCE_CM) {
    ++stats_.parse_error_count;
    return false;
  }

  commitFrame(distance_cm, angle_raw, rssi_dbm, now_ms);
  return true;
}

void UwbGcP2304Parser::commitFrame(uint16_t distance_cm, int16_t angle_raw_deg,
                                   int rssi_dbm, uint32_t now_ms) {
  ++stats_.frame_count;
  stats_.last_frame_ms = now_ms;
  stats_.last_raw_distance_cm = distance_cm;
  stats_.last_raw_angle_deg = angle_raw_deg;
  stats_.last_rssi_dbm = rssi_dbm;

  const float distance_cm_f = static_cast<float>(distance_cm);
  const float bearing_in = normalizeAngleDeg(static_cast<float>(angle_raw_deg));

  if (!filter_init_) {
    filter_init_ = true;
    ema_distance_cm_ = distance_cm_f;
    ema_bearing_deg_ = bearing_in;
  } else {
    const float d_alpha = profile::UWB_DISTANCE_EMA_ALPHA;
    ema_distance_cm_ =
        d_alpha * distance_cm_f + (1.0f - d_alpha) * ema_distance_cm_;

    // Wrap-safe bearing blend: work on the shortest signed delta, then wrap.
    float delta = normalizeAngleDeg(bearing_in - ema_bearing_deg_);
    const float b_alpha = std::fabs(delta) > profile::UWB_BEARING_FAST_THRESHOLD_DEG
                              ? profile::UWB_BEARING_EMA_ALPHA_FAST
                              : profile::UWB_BEARING_EMA_ALPHA_SLOW;
    ema_bearing_deg_ = normalizeAngleDeg(ema_bearing_deg_ + b_alpha * delta);
  }

  target_.valid = true;
  target_.last_update_ms = now_ms;
  target_.distance_mm = static_cast<int>(std::lround(ema_distance_cm_ * 10.0f));
  target_.bearing_deg = ema_bearing_deg_;
  target_.confidence = rssiToConfidence(rssi_dbm);
}

void UwbGcP2304Parser::update(uint32_t now_ms) {
  if (!target_.valid) {
    return;
  }
  if (elapsedMs(now_ms, target_.last_update_ms) > profile::UWB_PARSER_TIMEOUT_MS) {
    target_.valid = false;
    target_.confidence = 0;
    filter_init_ = false;
  }
}

float UwbGcP2304Parser::normalizeAngleDeg(float deg) {
  while (deg > 180.0f) {
    deg -= 360.0f;
  }
  while (deg < -180.0f) {
    deg += 360.0f;
  }
  return deg;
}

uint8_t UwbGcP2304Parser::rssiToConfidence(int rssi_dbm) {
  const int lo = profile::UWB_RSSI_MIN_DBM;
  const int hi = profile::UWB_RSSI_MAX_DBM;
  if (rssi_dbm <= lo) {
    return 0;
  }
  if (rssi_dbm >= hi) {
    return 255;
  }
  const float scaled =
      255.0f * static_cast<float>(rssi_dbm - lo) / static_cast<float>(hi - lo);
  return static_cast<uint8_t>(std::lround(scaled));
}

}  // namespace followbox
