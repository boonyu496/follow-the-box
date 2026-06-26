#include "control/rc_input_ds600.h"

#include <cstdint>
#include <cmath>

#include "config/board_pins.h"
#include "config/profile_defaults.h"
#include "core/math_utils.h"
#include "core/time_utils.h"

namespace followbox {
namespace {

constexpr uint16_t kMinPulseUs = 900;
constexpr uint16_t kMaxPulseUs = 2100;
constexpr uint16_t kLowUs = 1000;
constexpr uint16_t kCenterUs = 1500;
constexpr uint16_t kHighUs = 2000;
constexpr uint16_t kDeadbandUs = 50;
constexpr uint16_t kSwitchLowUs = 1300;
constexpr uint16_t kSwitchHighUs = 1700;
constexpr uint32_t kChannelStaleMs = 100;

bool validPulse(uint16_t pulse_us) {
  return pulse_us >= kMinPulseUs && pulse_us <= kMaxPulseUs;
}

uint32_t channelAgeMs(uint32_t now_ms, uint32_t last_update_ms) {
  if (last_update_ms == 0) {
    return 0;
  }
  return elapsedMsClamped(now_ms, last_update_ms);
}

}  // namespace

bool RcInputDs600::begin() {
  ch1_ = PwmInput(pins::PIN_RC_CH1_STEERING);
  ch2_ = PwmInput(pins::PIN_RC_CH2_THROTTLE);
  ch3_ = PwmInput(pins::PIN_RC_CH3_SPEED);
  ch4_ = PwmInput(pins::PIN_RC_CH4_MODE);
  ch5_ = PwmInput(pins::PIN_RC_CH5_STOP);
  const bool ok = ch1_.begin() && ch2_.begin() && ch3_.begin() && ch4_.begin() &&
                  ch5_.begin();
  snapshot_ = RcInput{};
  return ok;
}

void RcInputDs600::update(uint32_t now_ms) {
  uint16_t channels[6] = {0, 0, 0, 0, 0, 0};
  uint32_t channel_age_ms[6] = {0, 0, 0, 0, 0, 0};
  uint32_t newest_update_ms = 0;

  const bool channels_valid =
      readChannels(now_ms, channels, channel_age_ms, newest_update_ms);
  for (uint8_t i = 0; i < 6; ++i) {
    snapshot_.ch_us[i] = channels[i];
    snapshot_.ch_age_ms[i] = channel_age_ms[i];
  }

  if (!channels_valid) {
    snapshot_.online = false;
    snapshot_.last_update_ms = newest_update_ms;
    snapshot_.throttle = 0.0f;
    snapshot_.steering = 0.0f;
    snapshot_.speed_limit = 0.0f;
    snapshot_.stop_switch = true;
    snapshot_.auto_request = false;
    return;
  }

  snapshot_.online = true;
  snapshot_.last_update_ms = newest_update_ms;
  snapshot_.steering = normalizeCentered(channels[0]);
  snapshot_.throttle = normalizeCentered(channels[1]);
  snapshot_.speed_limit = normalizeSpeedLimit(channels[2]);
  snapshot_.auto_request = channels[3] > kSwitchHighUs;
  snapshot_.stop_switch = channels[4] > kSwitchHighUs;
}

bool RcInputDs600::isOnline(uint32_t now_ms) const {
  return snapshot_.online &&
         !isStale(now_ms, snapshot_.last_update_ms,
                  profile::PHYSICAL_REMOTE_LOST_STOP_MS);
}

bool RcInputDs600::readChannels(uint32_t now_ms, uint16_t (&channels)[6],
                                uint32_t (&channel_age_ms)[6],
                                uint32_t& newest_update_ms) const {
  const PwmInputSnapshot pwm[] = {
      ch1_.snapshot(),
      ch2_.snapshot(),
      ch3_.snapshot(),
      ch4_.snapshot(),
      ch5_.snapshot(),
  };

  newest_update_ms = 0;
  uint32_t newest_age_ms = UINT32_MAX;
  bool all_valid = true;
  for (uint8_t i = 0; i < 5; ++i) {
    channels[i] = pwm[i].pulse_us;
    channel_age_ms[i] = channelAgeMs(now_ms, pwm[i].last_update_ms);
    if (pwm[i].last_update_ms > 0 && channel_age_ms[i] <= newest_age_ms) {
      newest_age_ms = channel_age_ms[i];
      newest_update_ms = pwm[i].last_update_ms;
    }
    if (!validPulse(pwm[i].pulse_us) ||
        isStale(now_ms, pwm[i].last_update_ms, kChannelStaleMs)) {
      all_valid = false;
    }
  }
  channels[5] = 0;
  return all_valid;
}

float RcInputDs600::normalizeCentered(uint16_t pulse_us) const {
  const int delta = static_cast<int>(pulse_us) - static_cast<int>(kCenterUs);
  if (std::abs(delta) <= kDeadbandUs) {
    return 0.0f;
  }
  const float denom =
      delta > 0 ? static_cast<float>(kHighUs - kCenterUs)
                : static_cast<float>(kCenterUs - kLowUs);
  return clampUnit(static_cast<float>(delta) / denom);
}

float RcInputDs600::normalizeSpeedLimit(uint16_t pulse_us) const {
  const float raw =
      (static_cast<float>(pulse_us) - static_cast<float>(kLowUs)) /
      static_cast<float>(kHighUs - kLowUs);
  return clampFloat(0.1f + raw * 0.9f, 0.1f, 1.0f);
}

}  // namespace followbox

