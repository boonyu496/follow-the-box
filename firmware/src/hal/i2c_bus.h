#pragma once

#include <cstdint>

namespace followbox {

class I2cBus {
 public:
  I2cBus(int sda_pin, int scl_pin, uint32_t frequency_hz = 400000);

  bool begin();
  bool busClear();

 private:
  // These helpers change GPIO mode and are safe only while Wire is stopped.
  bool isSdaReleased() const;
  bool isSclReleased() const;
  void driveScl(bool high) const;
  void driveSda(bool high) const;

  int sda_pin_ = -1;
  int scl_pin_ = -1;
  uint32_t frequency_hz_ = 400000;
};

}  // namespace followbox

