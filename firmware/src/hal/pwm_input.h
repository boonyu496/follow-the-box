#pragma once

#include <cstdint>

namespace followbox {

struct PwmInputSnapshot {
  uint16_t pulse_us = 0;
  uint32_t last_update_ms = 0;
};

class PwmInput {
 public:
  explicit PwmInput(int pin = -1);

  bool begin();
  PwmInputSnapshot snapshot() const;
  bool isValid() const { return pin_ >= 0; }

 private:
  static void handleInterrupt(void* arg);
  void onEdge();

  int pin_ = -1;
  volatile uint32_t rising_edge_us_ = 0;
  volatile uint16_t pulse_us_ = 0;
  volatile uint32_t last_update_ms_ = 0;
};

}  // namespace followbox

