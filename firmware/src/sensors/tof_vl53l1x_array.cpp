#include "sensors/tof_vl53l1x_array.h"

#include <Arduino.h>
#include <VL53L1X.h>
#include <Wire.h>

#include "config/board_pins.h"
#include "config/profile_defaults.h"
#include "core/time_utils.h"
#include "hal/i2c_bus.h"

namespace followbox {
namespace {

// The three VL53L1X live at file scope (not in the header) so the public API
// stays free of the Arduino/library types, matching the HAL-in-cpp convention.
// There is exactly one TofVl53l1xArray.
constexpr uint8_t kChannelCount = 3;
const uint8_t kChannels[kChannelCount] = {
    profile::TOF_CHANNEL_FRONT_CENTER,
    profile::TOF_CHANNEL_FRONT_LEFT,
    profile::TOF_CHANNEL_FRONT_RIGHT,
};

VL53L1X g_sensors[kChannelCount];
bool g_channel_ready[kChannelCount] = {false, false, false};
I2cBus g_i2c_bus(pins::PIN_I2C_SDA, pins::PIN_I2C_SCL);

// Select one TCA9548A downstream channel (exclusive). Returns false on NACK.
bool selectMuxChannel(uint8_t channel) {
  Wire.beginTransmission(profile::TOF_TCA9548A_ADDR);
  Wire.write(static_cast<uint8_t>(1u << channel));
  return Wire.endTransmission() == 0;
}

bool initSensorOnChannel(uint8_t channel) {
  if (!selectMuxChannel(channel)) {
    return false;
  }
  VL53L1X& sensor = g_sensors[channel];
  sensor.setBus(&Wire);
  sensor.setTimeout(profile::TOF_CONTINUOUS_PERIOD_MS * 2);
  if (!sensor.init()) {
    return false;
  }
  sensor.setDistanceMode(VL53L1X::Long);
  sensor.setMeasurementTimingBudget(profile::TOF_TIMING_BUDGET_US);
  sensor.startContinuous(profile::TOF_CONTINUOUS_PERIOD_MS);
  return true;
}

}  // namespace

void TofVl53l1xArray::begin() {
  snapshot_ = TofSnapshot{};
  stats_ = TofStats{};
  next_channel_ = 0;
  initialised_ = false;
  consecutive_failures_ = 0;
  last_recovery_attempt_ms_ = 0;
  for (uint8_t i = 0; i < kChannelCount; ++i) g_channel_ready[i] = false;

  // First prototype has no XSHUT: clear the bus before probing so a sensor that
  // latched SDA low from a prior boot does not wedge the whole mux.
  if (!g_i2c_bus.begin()) {
    g_i2c_bus.busClear();
    stats_.bus_clear_count++;
  }
  Wire.setClock(400000);

  for (uint8_t i = 0; i < kChannelCount; ++i) {
    const uint8_t channel = kChannels[i];
    const bool ok = initSensorOnChannel(channel);
    g_channel_ready[channel] = ok;
    if (ok) {
      stats_.init_ok_mask |= (1u << channel);
      initialised_ = true;
    }
  }
}

void TofVl53l1xArray::update(uint32_t now_ms) {
  invalidateStale(now_ms);
  serviceRecovery(now_ms);
  if (!initialised_) {
    return;
  }

  // Round-robin one channel per call to keep each update bounded and non-blocking.
  const uint8_t channel = kChannels[next_channel_];
  next_channel_ = static_cast<uint8_t>((next_channel_ + 1) % kChannelCount);

  if (!g_channel_ready[channel]) {
    applyChannelReading(channel, -1, now_ms);
    return;
  }
  if (!selectMuxChannel(channel)) {
    stats_.mux_nack_count++;
    markChannelFailed(channel, now_ms);
    applyChannelReading(channel, -1, now_ms);
    return;
  }

  VL53L1X& sensor = g_sensors[channel];
  if (!sensor.dataReady()) {
    return;  // no new range yet; leave the prior reading until the stale timeout
  }

  const uint16_t raw_mm = sensor.read(false);
  if (sensor.timeoutOccurred()) {
    stats_.timeout_count++;
    markChannelFailed(channel, now_ms);
    applyChannelReading(channel, -1, now_ms);
    return;
  }

  consecutive_failures_ = 0;
  stats_.read_count++;
  stats_.last_read_ms = now_ms;
  applyChannelReading(channel, static_cast<int>(raw_mm), now_ms);
}

void TofVl53l1xArray::applyChannelReading(uint8_t channel, int distance_mm,
                                          uint32_t now_ms) {
  const bool in_range = distance_mm >= profile::TOF_MIN_VALID_MM &&
                        distance_mm <= profile::TOF_MAX_VALID_MM;

  if (channel == profile::TOF_CHANNEL_FRONT_CENTER) {
    snapshot_.front_center_valid = in_range;
    if (in_range) snapshot_.front_center_mm = distance_mm;
  } else if (channel == profile::TOF_CHANNEL_FRONT_LEFT) {
    snapshot_.front_left_valid = in_range;
    if (in_range) snapshot_.front_left_mm = distance_mm;
  } else if (channel == profile::TOF_CHANNEL_FRONT_RIGHT) {
    snapshot_.front_right_valid = in_range;
    if (in_range) snapshot_.front_right_mm = distance_mm;
  }

  if (in_range) {
    snapshot_.last_update_ms = now_ms;
  }

  invalidateStale(now_ms);
}

void TofVl53l1xArray::invalidateStale(uint32_t now_ms) {
  // Invalidate the whole array if every channel stopped delivering fresh ranges.
  if (isStale(now_ms, snapshot_.last_update_ms, profile::TOF_STALE_TIMEOUT_MS)) {
    snapshot_.front_left_valid = false;
    snapshot_.front_center_valid = false;
    snapshot_.front_right_valid = false;
  }
  snapshot_.valid = snapshot_.front_left_valid || snapshot_.front_center_valid ||
                    snapshot_.front_right_valid;
}

void TofVl53l1xArray::markChannelFailed(uint8_t channel, uint32_t now_ms) {
  if (channel < kChannelCount) {
    g_channel_ready[channel] = false;
    stats_.init_ok_mask &= ~(1u << channel);
  }
  initialised_ = g_channel_ready[0] || g_channel_ready[1] || g_channel_ready[2];
  if (++consecutive_failures_ < profile::TOF_FAILURES_BEFORE_BUS_CLEAR) return;

  // Releasing the bus is bounded to nine SCL pulses. Re-initialisation is
  // deliberately deferred and limited to one channel per recovery interval.
  g_i2c_bus.busClear();
  Wire.setClock(400000);
  stats_.bus_clear_count++;
  stats_.last_recovery_ms = now_ms;
  consecutive_failures_ = 0;
  initialised_ = false;
  stats_.init_ok_mask = 0;
  for (uint8_t i = 0; i < kChannelCount; ++i) g_channel_ready[i] = false;
}

void TofVl53l1xArray::serviceRecovery(uint32_t now_ms) {
  bool missing = false;
  for (uint8_t i = 0; i < kChannelCount; ++i) {
    if (!g_channel_ready[kChannels[i]]) {
      missing = true;
      break;
    }
  }
  if (!missing ||
      (last_recovery_attempt_ms_ != 0 &&
       elapsedMs(now_ms, last_recovery_attempt_ms_) <
           profile::TOF_REINIT_INTERVAL_MS)) {
    return;
  }

  last_recovery_attempt_ms_ = now_ms;
  for (uint8_t i = 0; i < kChannelCount; ++i) {
    const uint8_t channel = kChannels[i];
    if (g_channel_ready[channel]) continue;
    const bool ok = initSensorOnChannel(channel);
    g_channel_ready[channel] = ok;
    if (ok) {
      stats_.init_ok_mask |= (1u << channel);
      stats_.reinit_count++;
      stats_.last_recovery_ms = now_ms;
      initialised_ = true;
    }
    break;
  }
}

}  // namespace followbox
