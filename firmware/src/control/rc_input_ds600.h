#pragma once

#include <cstdint>

#include "core/types.h"
#include "hal/pwm_input.h"

namespace followbox {

class RcInputDs600 {
 public:
  bool begin();
  void update(uint32_t now_ms);
  RcInput getSnapshot() const { return snapshot_; }
  bool isOnline(uint32_t now_ms) const;

 private:
  bool readChannels(uint32_t now_ms, uint16_t (&channels)[6],
                    uint32_t& newest_update_ms) const;
  float normalizeCentered(uint16_t pulse_us) const;
  float normalizeSpeedLimit(uint16_t pulse_us) const;

  PwmInput ch1_;
  PwmInput ch2_;
  PwmInput ch3_;
  PwmInput ch4_;
  PwmInput ch5_;
  RcInput snapshot_;
};

}  // namespace followbox

