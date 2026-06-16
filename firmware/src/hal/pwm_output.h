#pragma once

#include <cstdint>

namespace followbox {

class PwmOutput {
 public:
  PwmOutput() = default;
  PwmOutput(int pin, int channel, uint32_t frequency_hz, uint8_t resolution_bits);

  void begin();
  void writeDuty(uint32_t duty);
  uint32_t maxDuty() const;
  bool isValid() const { return pin_ >= 0 && channel_ >= 0; }

 private:
  int pin_ = -1;
  int channel_ = -1;
  uint32_t frequency_hz_ = 1000;
  uint8_t resolution_bits_ = 12;
};

}  // namespace followbox

