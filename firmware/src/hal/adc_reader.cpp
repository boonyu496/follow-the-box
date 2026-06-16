#include "hal/adc_reader.h"

#include <Arduino.h>

namespace followbox {

AdcReader::AdcReader(int pin) : pin_(pin) {}

void AdcReader::begin() {
  if (!isValid()) {
    return;
  }
  pinMode(pin_, INPUT);
  analogSetPinAttenuation(pin_, ADC_11db);
}

uint32_t AdcReader::readMillivolts(uint8_t samples) const {
  if (!isValid() || samples == 0) {
    return 0;
  }

  uint32_t sum = 0;
  for (uint8_t i = 0; i < samples; ++i) {
    sum += static_cast<uint32_t>(analogReadMilliVolts(pin_));
  }
  return sum / samples;
}

}  // namespace followbox

