#pragma once

#include <cstdint>

namespace followbox {

class AdcReader {
 public:
  explicit AdcReader(int pin = -1);

  void begin();
  uint32_t readMillivolts(uint8_t samples = 10) const;
  bool isValid() const { return pin_ >= 0; }

 private:
  int pin_ = -1;
};

}  // namespace followbox

