#include "hal/gpio_in.h"

#include <Arduino.h>

namespace followbox {

GpioIn::GpioIn(int pin, ActiveLevel active_level, InputPull pull)
    : pin_(pin), active_level_(active_level), pull_(pull) {}

void GpioIn::begin() {
  if (!isValid()) {
    return;
  }
  switch (pull_) {
    case InputPull::PULL_UP:
      pinMode(pin_, INPUT_PULLUP);
      break;
    case InputPull::PULL_DOWN:
      pinMode(pin_, INPUT_PULLDOWN);
      break;
    case InputPull::FLOATING:
    default:
      pinMode(pin_, INPUT);
      break;
  }
}

bool GpioIn::readActive() const {
  if (!isValid()) {
    return false;
  }
  const int raw = digitalRead(pin_);
  return active_level_ == ActiveLevel::ACTIVE_HIGH ? raw == HIGH : raw == LOW;
}

}  // namespace followbox
