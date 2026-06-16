#pragma once

#include <cstdint>

namespace followbox {

class I2cBus {
 public:
  I2cBus(int sda_pin, int scl_pin, uint32_t frequency_hz = 400000);

  bool begin();
  bool busClear();
  bool isSdaReleased() const;
  bool isSclReleased() const;

 private:
  void driveScl(bool high) const;
  void driveSda(bool high) const;

  int sda_pin_ = -1;
  int scl_pin_ = -1;
  uint32_t frequency_hz_ = 400000;
};

}  // namespace followbox

