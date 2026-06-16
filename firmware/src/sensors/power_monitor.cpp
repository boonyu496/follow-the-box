#include "sensors/power_monitor.h"

#include "config/board_pins.h"
#include "config/profile_defaults.h"
#include "core/time_utils.h"

namespace followbox {

PowerMonitor::PowerMonitor()
    : battery_adc_(pins::PIN_BATTERY_ADC),
      controller_fault_(pins::PIN_CONTROLLER_FAULT, ActiveLevel::ACTIVE_LOW,
                        InputPull::FLOATING) {}

bool PowerMonitor::begin() {
  battery_adc_.begin();
  controller_fault_.begin();
  snapshot_ = PowerStatus{};
  return battery_adc_.isValid();
}

void PowerMonitor::update(uint32_t now_ms) {
  const uint32_t adc_mv = battery_adc_.readMillivolts(10);
  snapshot_.valid = adc_mv > 0;
  snapshot_.last_update_ms = now_ms;
  snapshot_.battery_voltage = packVoltageFromAdcMillivolts(adc_mv);
  snapshot_.low_battery =
      snapshot_.valid && snapshot_.battery_voltage <= profile::BATTERY_LOW_VOLTAGE;

  const bool controller_fault_active =
      controller_fault_.isValid() && controller_fault_.readActive();
  snapshot_.motor_fault_left = controller_fault_active;
  snapshot_.motor_fault_right = controller_fault_active;
}

bool PowerMonitor::isOnline(uint32_t now_ms) const {
  return snapshot_.valid && !isStale(now_ms, snapshot_.last_update_ms, 1000);
}

float PowerMonitor::packVoltageFromAdcMillivolts(uint32_t adc_mv) const {
  const float divider =
      static_cast<float>(profile::BATTERY_DIVIDER_TOP_OHM +
                         profile::BATTERY_DIVIDER_BOTTOM_OHM) /
      static_cast<float>(profile::BATTERY_DIVIDER_BOTTOM_OHM);
  return (static_cast<float>(adc_mv) * divider) / 1000.0f;
}

}  // namespace followbox

