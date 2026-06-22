#include "hal/uart_bus.h"

#include <Arduino.h>

namespace followbox {

UartBus::UartBus(int uart_num, int rx_pin, int tx_pin, uint32_t baud)
    : uart_num_(uart_num), rx_pin_(rx_pin), tx_pin_(tx_pin), baud_(baud) {}

bool UartBus::begin() {
  if (!isEnabled() || started_) {
    return started_;
  }

  // Reuse the framework's predefined UART globals. With USB-CDC enabled,
  // UART0 can be remapped to sensor pins when explicitly selected.
  switch (uart_num_) {
    case 0:
      serial_ = &Serial0;
      break;
    case 1:
      serial_ = &Serial1;
      break;
    case 2:
      serial_ = &Serial2;
      break;
    default:
      return false;
  }

  serial_->begin(baud_, SERIAL_8N1, rx_pin_, tx_pin_);
  started_ = true;
  return true;
}

int UartBus::available() {
  if (!started_ || serial_ == nullptr) {
    return 0;
  }
  return serial_->available();
}

int UartBus::read() {
  if (!started_ || serial_ == nullptr) {
    return -1;
  }
  return serial_->read();
}

size_t UartBus::write(const uint8_t* data, size_t length) {
  if (!started_ || serial_ == nullptr || data == nullptr || length == 0) {
    return 0;
  }
  return serial_->write(data, length);
}

}  // namespace followbox
