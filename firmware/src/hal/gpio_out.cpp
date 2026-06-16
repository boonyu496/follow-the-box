#include "hal/gpio_out.h"

#include <Arduino.h>

namespace followbox {

GpioOut::GpioOut(int pin, ActiveLevel active_level)
    : pin_(pin), active_level_(active_level) {}

void GpioOut::begin(bool active) {
  if (!isValid()) {
    return;
  }
  pinMode(pin_, OUTPUT);
  writeActive(active);
}

void GpioOut::writeActive(bool active) {
  if (!isValid()) {
    return;
  }
  digitalWrite(pin_, active ? activeLevel() : inactiveLevel());
}

int GpioOut::inactiveLevel() const {
  return active_level_ == ActiveLevel::ACTIVE_HIGH ? LOW : HIGH;
}

int GpioOut::activeLevel() const {
  return active_level_ == ActiveLevel::ACTIVE_HIGH ? HIGH : LOW;
}

}  // namespace followbox
