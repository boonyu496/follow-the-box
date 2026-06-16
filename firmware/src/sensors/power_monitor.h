#pragma once

#include <cstdint>

#include "core/types.h"
#include "hal/adc_reader.h"
#include "hal/gpio_in.h"

namespace followbox {

class PowerMonitor {
 public:
  PowerMonitor();

  bool begin();
  void update(uint32_t now_ms);
  PowerStatus getSnapshot() const { return snapshot_; }
  bool isOnline(uint32_t now_ms) const;

 private:
  float packVoltageFromAdcMillivolts(uint32_t adc_mv) const;

  AdcReader battery_adc_;
  GpioIn controller_fault_;
  PowerStatus snapshot_;
};

}  // namespace followbox

