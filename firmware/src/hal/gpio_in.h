#pragma once

#include "hal/gpio_out.h"

namespace followbox {

enum class InputPull {
  FLOATING,
  PULL_UP,
  PULL_DOWN
};

class GpioIn {
 public:
  GpioIn() = default;
  GpioIn(int pin, ActiveLevel active_level, InputPull pull = InputPull::FLOATING);

  void begin();
  bool readActive() const;
  bool isValid() const { return pin_ >= 0; }

 private:
  int pin_ = -1;
  ActiveLevel active_level_ = ActiveLevel::ACTIVE_HIGH;
  InputPull pull_ = InputPull::FLOATING;
};

}  // namespace followbox
