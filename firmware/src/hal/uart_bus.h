#pragma once

#include <cstdint>

class HardwareSerial;

namespace followbox {

// Thin Arduino HardwareSerial wrapper for read-only sensor UART streams.
//
// Pure transport only: it never parses bytes and never touches the motion path.
// Callers (sensor_task) pull raw bytes and feed them to the pure-logic parsers.
// A negative rx_pin disables the bus (begin() is a no-op, read() returns -1),
// which lets optional sensors stay unconfigured without inventing a GPIO.
class UartBus {
 public:
  UartBus(int uart_num, int rx_pin, int tx_pin, uint32_t baud);

  bool begin();
  bool isEnabled() const { return rx_pin_ >= 0; }

  // Number of bytes currently buffered (0 when disabled).
  int available();
  // Next received byte, or -1 when none / disabled.
  int read();

 private:
  HardwareSerial* serial_ = nullptr;
  int uart_num_ = -1;
  int rx_pin_ = -1;
  int tx_pin_ = -1;
  uint32_t baud_ = 0;
  bool started_ = false;
};

}  // namespace followbox
