#pragma once

namespace followbox {

enum class ActiveLevel {
  ACTIVE_LOW,
  ACTIVE_HIGH
};

class GpioOut {
 public:
  GpioOut() = default;
  GpioOut(int pin, ActiveLevel active_level);

  void begin(bool active = false);
  void writeActive(bool active);
  bool isValid() const { return pin_ >= 0; }

 private:
  int inactiveLevel() const;
  int activeLevel() const;

  int pin_ = -1;
  ActiveLevel active_level_ = ActiveLevel::ACTIVE_HIGH;
};

}  // namespace followbox
