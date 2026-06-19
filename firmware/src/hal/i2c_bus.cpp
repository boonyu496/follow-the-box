#include "hal/i2c_bus.h"

#include <Arduino.h>
#include <Wire.h>

namespace followbox {
namespace {

constexpr uint16_t kBusClearPulseDelayUs = 5;
constexpr uint8_t kBusClearPulses = 9;

}  // namespace

I2cBus::I2cBus(int sda_pin, int scl_pin, uint32_t frequency_hz)
    : sda_pin_(sda_pin), scl_pin_(scl_pin), frequency_hz_(frequency_hz) {}

bool I2cBus::begin() {
  if (sda_pin_ < 0 || scl_pin_ < 0) {
    return false;
  }

  // Sample the released bus before attaching the pins to the I2C peripheral.
  // Calling pinMode() after Wire.begin() changes the ESP32 GPIO direction and
  // can leave the controller unable to drive SDA/SCL, making every device look
  // like an address NACK.
  const bool lines_released = isSdaReleased() && isSclReleased();
  if (!Wire.begin(sda_pin_, scl_pin_, frequency_hz_)) {
    return false;
  }
  Wire.setClock(frequency_hz_);
  return lines_released;
}

bool I2cBus::busClear() {
  if (sda_pin_ < 0 || scl_pin_ < 0) {
    return false;
  }

  Wire.end();
  pinMode(sda_pin_, INPUT_PULLUP);
  pinMode(scl_pin_, INPUT_PULLUP);
  delayMicroseconds(kBusClearPulseDelayUs);

  for (uint8_t i = 0; i < kBusClearPulses && !isSdaReleased(); ++i) {
    driveScl(false);
    delayMicroseconds(kBusClearPulseDelayUs);
    driveScl(true);
    delayMicroseconds(kBusClearPulseDelayUs);
  }

  driveSda(false);
  delayMicroseconds(kBusClearPulseDelayUs);
  driveScl(true);
  delayMicroseconds(kBusClearPulseDelayUs);
  driveSda(true);
  delayMicroseconds(kBusClearPulseDelayUs);

  return begin();
}

bool I2cBus::isSdaReleased() const {
  pinMode(sda_pin_, INPUT_PULLUP);
  return digitalRead(sda_pin_) == HIGH;
}

bool I2cBus::isSclReleased() const {
  pinMode(scl_pin_, INPUT_PULLUP);
  return digitalRead(scl_pin_) == HIGH;
}

void I2cBus::driveScl(bool high) const {
  if (high) {
    pinMode(scl_pin_, INPUT_PULLUP);
  } else {
    pinMode(scl_pin_, OUTPUT);
    digitalWrite(scl_pin_, LOW);
  }
}

void I2cBus::driveSda(bool high) const {
  if (high) {
    pinMode(sda_pin_, INPUT_PULLUP);
  } else {
    pinMode(sda_pin_, OUTPUT);
    digitalWrite(sda_pin_, LOW);
  }
}

}  // namespace followbox

