#include "hal/pwm_output.h"

#include <Arduino.h>

namespace followbox {

PwmOutput::PwmOutput(int pin, int channel, uint32_t frequency_hz,
                     uint8_t resolution_bits)
    : pin_(pin),
      channel_(channel),
      frequency_hz_(frequency_hz),
      resolution_bits_(resolution_bits) {}

void PwmOutput::begin() {
  if (!isValid()) {
    return;
  }
  ledcSetup(channel_, frequency_hz_, resolution_bits_);
  ledcAttachPin(pin_, channel_);
  writeDuty(0);
}

void PwmOutput::writeDuty(uint32_t duty) {
  if (!isValid()) {
    return;
  }
  ledcWrite(channel_, duty > maxDuty() ? maxDuty() : duty);
}

uint32_t PwmOutput::maxDuty() const {
  return (1UL << resolution_bits_) - 1UL;
}

}  // namespace followbox

