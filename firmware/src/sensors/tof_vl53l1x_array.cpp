#include "sensors/tof_vl53l1x_array.h"

#include <Arduino.h>
#include <VL53L1X.h>
#include <Wire.h>

#include "config/board_pins.h"
#include "config/profile_defaults.h"
#include "core/time_utils.h"
#include "hal/i2c_bus.h"
#include "telemetry/debug_console.h"

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
I2cBus g_i2c_bus(pins::PIN_I2C_SDA, pins::PIN_I2C_SCL,
                 profile::TOF_I2C_CLOCK_HZ);
uint8_t g_tca_addr = profile::TOF_TCA9548A_ADDR;
bool g_logged_full_bus_scan = false;
bool g_logged_swapped_pin_scan = false;

enum class InitResult : uint8_t {
  kOk,
  kMuxNack,
  kSensorInitFailed,
};

struct InitOutcome {
  InitResult result = InitResult::kOk;
  uint8_t wire_error = 0;
};

struct I2cScanSummary {
  uint8_t ack_count = 0;
  uint8_t first_ack_addr = 0;
  uint8_t addr_nack_count = 0;
  uint8_t data_nack_count = 0;
  uint8_t bus_error_count = 0;
  uint8_t timeout_count = 0;
  uint8_t other_error_count = 0;
};

// Select one TCA9548A downstream channel (exclusive). Returns the Wire error
// code: 0=ACK, 2=address NACK, 3=data NACK, 4=other bus error, 5=timeout.
uint8_t selectMuxChannelRaw(uint8_t channel) {
  Wire.beginTransmission(g_tca_addr);
  Wire.write(static_cast<uint8_t>(1u << channel));
  return Wire.endTransmission();
}

bool selectMuxChannel(uint8_t channel) {
  return selectMuxChannelRaw(channel) == 0;
}

uint8_t probeAddress(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission();
}

void tallyI2cProbe(uint8_t address, uint8_t err, I2cScanSummary& summary) {
  if (err == 0) {
    if (summary.first_ack_addr == 0) summary.first_ack_addr = address;
    ++summary.ack_count;
  } else if (err == 2) {
    ++summary.addr_nack_count;
  } else if (err == 3) {
    ++summary.data_nack_count;
  } else if (err == 4) {
    ++summary.bus_error_count;
  } else if (err == 5) {
    ++summary.timeout_count;
  } else {
    ++summary.other_error_count;
  }
}

bool shouldLogFailure(uint32_t count) {
  return count <= 3 || count % 10 == 0;
}

void restoreTofI2cClock() {
  Wire.setClock(profile::TOF_I2C_CLOCK_HZ);
}

void logI2cLevels(const char* phase) {
  FB_LOGW("TOF i2c %s: SDA=GPIO%d level=%d SCL=GPIO%d level=%d",
          phase, pins::PIN_I2C_SDA, digitalRead(pins::PIN_I2C_SDA),
          pins::PIN_I2C_SCL, digitalRead(pins::PIN_I2C_SCL));
}

void logFullBusScanOnce() {
  if (g_logged_full_bus_scan) return;
  g_logged_full_bus_scan = true;

  I2cScanSummary summary;
  for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
    const uint8_t err = probeAddress(addr);
    tallyI2cProbe(addr, err, summary);
    if (err == 0) {
      FB_LOGW("TOF scan-all: ACK addr=0x%02x", addr);
    }
  }

  if (summary.ack_count == 0) {
    FB_LOGW(
        "TOF scan-all: no ACK devices addr_nack=%u data_nack=%u bus_err=%u "
        "timeout=%u other=%u",
        summary.addr_nack_count, summary.data_nack_count,
        summary.bus_error_count, summary.timeout_count,
        summary.other_error_count);
  }
}

void logSwappedPinScanOnce() {
  if (g_logged_swapped_pin_scan) return;
  g_logged_swapped_pin_scan = true;

  FB_LOGW("TOF swap-check: scanning once with SDA=GPIO%d SCL=GPIO%d",
          pins::PIN_I2C_SCL, pins::PIN_I2C_SDA);
  Wire.end();
  if (!Wire.begin(pins::PIN_I2C_SCL, pins::PIN_I2C_SDA,
                  profile::TOF_I2C_CLOCK_HZ)) {
    FB_LOGW("TOF swap-check: Wire.begin failed");
    g_i2c_bus.begin();
    restoreTofI2cClock();
    return;
  }
  restoreTofI2cClock();

  I2cScanSummary summary;
  for (uint8_t addr = 0x70; addr <= 0x77; ++addr) {
    const uint8_t err = probeAddress(addr);
    tallyI2cProbe(addr, err, summary);
    if (err == 0) {
      FB_LOGW(
          "TOF swap-check: ACK addr=0x%02x with reversed pins; verify GPIO10=SDA "
          "and GPIO11=SCL wiring",
          addr);
    }
  }
  if (summary.ack_count == 0) {
    FB_LOGW(
        "TOF swap-check: no ACK with reversed pins addr_nack=%u data_nack=%u "
        "bus_err=%u timeout=%u other=%u",
        summary.addr_nack_count, summary.data_nack_count,
        summary.bus_error_count, summary.timeout_count,
        summary.other_error_count);
  }

  Wire.end();
  if (!g_i2c_bus.begin()) {
    FB_LOGW("TOF swap-check: restore normal I2C bus saw unreleased lines");
  }
  restoreTofI2cClock();
}

void detectAndLogTcaAddress() {
  I2cScanSummary summary;
  for (uint8_t addr = 0x70; addr <= 0x77; ++addr) {
    const uint8_t err = probeAddress(addr);
    tallyI2cProbe(addr, err, summary);
    if (err == 0) {
      FB_LOGI("TOF scan: TCA candidate ACK addr=0x%02x", addr);
    }
  }
  if (summary.ack_count == 0) {
    FB_LOGW(
        "TOF scan: no TCA9548A ACK in 0x70-0x77 (cfg=0x%02x) "
        "addr_nack=%u data_nack=%u bus_err=%u timeout=%u other=%u",
        profile::TOF_TCA9548A_ADDR, summary.addr_nack_count,
        summary.data_nack_count, summary.bus_error_count, summary.timeout_count,
        summary.other_error_count);
    logI2cLevels("after-scan");
    logFullBusScanOnce();
    logSwappedPinScanOnce();
    g_tca_addr = profile::TOF_TCA9548A_ADDR;
    return;
  }
  g_tca_addr = summary.first_ack_addr;
  if (summary.ack_count > 1) {
    FB_LOGW("TOF scan: multiple TCA-range ACKs count=%u using=0x%02x",
            summary.ack_count, g_tca_addr);
  }
  if (g_tca_addr != profile::TOF_TCA9548A_ADDR) {
    FB_LOGW("TOF scan: using detected TCA addr=0x%02x (cfg=0x%02x)",
            g_tca_addr, profile::TOF_TCA9548A_ADDR);
  }
}

InitOutcome initSensorOnChannel(uint8_t channel) {
  const uint8_t mux_err = selectMuxChannelRaw(channel);
  if (mux_err != 0) {
    return {InitResult::kMuxNack, mux_err};
  }
  VL53L1X& sensor = g_sensors[channel];
  sensor.setBus(&Wire);
  sensor.setTimeout(profile::TOF_CONTINUOUS_PERIOD_MS * 2);
  if (!sensor.init()) {
    return {InitResult::kSensorInitFailed, 0};
  }
  sensor.setDistanceMode(VL53L1X::Long);
  sensor.setMeasurementTimingBudget(profile::TOF_TIMING_BUDGET_US);
  sensor.startContinuous(profile::TOF_CONTINUOUS_PERIOD_MS);
  return {InitResult::kOk, 0};
}

}  // namespace

void TofVl53l1xArray::begin() {
  snapshot_ = TofSnapshot{};
  stats_ = TofStats{};
  next_channel_ = 0;
  initialised_ = false;
  consecutive_failures_ = 0;
  last_recovery_attempt_ms_ = 0;
  g_tca_addr = profile::TOF_TCA9548A_ADDR;
  g_logged_full_bus_scan = false;
  g_logged_swapped_pin_scan = false;
  for (uint8_t i = 0; i < kChannelCount; ++i) g_channel_ready[i] = false;

  // First prototype has no XSHUT: clear the bus before probing so a sensor that
  // latched SDA low from a prior boot does not wedge the whole mux.
  pinMode(pins::PIN_I2C_SDA, INPUT_PULLUP);
  pinMode(pins::PIN_I2C_SCL, INPUT_PULLUP);
  FB_LOGI("TOF begin: SDA=GPIO%d level=%d SCL=GPIO%d level=%d TCA=0x%02x",
          pins::PIN_I2C_SDA, digitalRead(pins::PIN_I2C_SDA),
          pins::PIN_I2C_SCL, digitalRead(pins::PIN_I2C_SCL),
          profile::TOF_TCA9548A_ADDR);

  if (!g_i2c_bus.begin()) {
    const bool recovered = g_i2c_bus.busClear();
    stats_.bus_clear_count++;
    FB_LOGW("TOF begin: I2C bus not released, bus_clear=%d", recovered ? 1 : 0);
  }
  restoreTofI2cClock();
  detectAndLogTcaAddress();

  for (uint8_t i = 0; i < kChannelCount; ++i) {
    const uint8_t channel = kChannels[i];
    stats_.init_attempt_count++;
    const InitOutcome outcome = initSensorOnChannel(channel);
    const InitResult result = outcome.result;
    const bool ok = result == InitResult::kOk;
    g_channel_ready[channel] = ok;
    if (ok) {
      stats_.init_ok_mask |= (1u << channel);
      initialised_ = true;
      FB_LOGI("TOF init ok ch=%u mask=0x%lx", channel,
              static_cast<unsigned long>(stats_.init_ok_mask));
    } else if (result == InitResult::kMuxNack) {
      stats_.mux_nack_count++;
      FB_LOGW("TOF init mux_nack ch=%u wire=%u nacks=%lu attempts=%lu",
              channel, outcome.wire_error,
              static_cast<unsigned long>(stats_.mux_nack_count),
              static_cast<unsigned long>(stats_.init_attempt_count));
    } else {
      stats_.init_failure_count++;
      FB_LOGW("TOF init sensor_fail ch=%u failures=%lu attempts=%lu",
              channel, static_cast<unsigned long>(stats_.init_failure_count),
              static_cast<unsigned long>(stats_.init_attempt_count));
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
    if (shouldLogFailure(stats_.mux_nack_count)) {
      FB_LOGW("TOF read mux_nack ch=%u nacks=%lu read=%lu",
              channel, static_cast<unsigned long>(stats_.mux_nack_count),
              static_cast<unsigned long>(stats_.read_count));
    }
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
    if (shouldLogFailure(stats_.timeout_count)) {
      FB_LOGW("TOF read timeout ch=%u timeouts=%lu",
              channel, static_cast<unsigned long>(stats_.timeout_count));
    }
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
  logI2cLevels("before-clear");
  g_i2c_bus.busClear();
  restoreTofI2cClock();
  detectAndLogTcaAddress();
  stats_.bus_clear_count++;
  stats_.last_recovery_ms = now_ms;
  FB_LOGW("TOF recovery: bus_clear count=%lu last_failure_ch=%u",
          static_cast<unsigned long>(stats_.bus_clear_count), channel);
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
    stats_.init_attempt_count++;
    const InitOutcome outcome = initSensorOnChannel(channel);
    const InitResult result = outcome.result;
    const bool ok = result == InitResult::kOk;
    g_channel_ready[channel] = ok;
    if (ok) {
      stats_.init_ok_mask |= (1u << channel);
      stats_.reinit_count++;
      stats_.last_recovery_ms = now_ms;
      initialised_ = true;
      consecutive_failures_ = 0;
      FB_LOGI("TOF reinit ok ch=%u mask=0x%lx reinit=%lu",
              channel, static_cast<unsigned long>(stats_.init_ok_mask),
              static_cast<unsigned long>(stats_.reinit_count));
    } else if (result == InitResult::kMuxNack) {
      stats_.mux_nack_count++;
      if (shouldLogFailure(stats_.mux_nack_count)) {
        FB_LOGW("TOF reinit mux_nack ch=%u wire=%u nacks=%lu attempts=%lu",
                channel, outcome.wire_error,
                static_cast<unsigned long>(stats_.mux_nack_count),
                static_cast<unsigned long>(stats_.init_attempt_count));
      }
      // A missing mux response can be a wedged shared bus. Reuse the bounded
      // recovery path after repeated failures; sensor-level init failures do
      // not clear the bus because they commonly mean power/wiring is absent.
      markChannelFailed(channel, now_ms);
    } else {
      stats_.init_failure_count++;
      if (shouldLogFailure(stats_.init_failure_count)) {
        FB_LOGW("TOF reinit sensor_fail ch=%u failures=%lu attempts=%lu",
                channel, static_cast<unsigned long>(stats_.init_failure_count),
                static_cast<unsigned long>(stats_.init_attempt_count));
      }
    }
    break;
  }
}

}  // namespace followbox
