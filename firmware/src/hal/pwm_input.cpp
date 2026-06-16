#include "hal/pwm_input.h"

#include <Arduino.h>

namespace followbox {

PwmInput::PwmInput(int pin) : pin_(pin) {}

bool PwmInput::begin() {
  if (!isValid()) {
    return false;
  }
  pinMode(pin_, INPUT);
  attachInterruptArg(pin_, &PwmInput::handleInterrupt, this, CHANGE);
  return true;
}

PwmInputSnapshot PwmInput::snapshot() const {
  noInterrupts();
  const uint16_t pulse_us = pulse_us_;
  const uint32_t last_update_ms = last_update_ms_;
  interrupts();
  return PwmInputSnapshot{pulse_us, last_update_ms};
}

void IRAM_ATTR PwmInput::handleInterrupt(void* arg) {
  static_cast<PwmInput*>(arg)->onEdge();
}

void IRAM_ATTR PwmInput::onEdge() {
  const uint32_t now_us = micros();
  if (digitalRead(pin_) == HIGH) {
    rising_edge_us_ = now_us;
    return;
  }

  const uint32_t width = static_cast<uint32_t>(now_us - rising_edge_us_);
  if (width <= 3000) {
    pulse_us_ = static_cast<uint16_t>(width);
    last_update_ms_ = now_us / 1000U;
  }
}

}  // namespace followbox

