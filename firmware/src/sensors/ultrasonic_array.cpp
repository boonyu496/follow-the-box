#include "sensors/ultrasonic_array.h"

#include <Arduino.h>

#include "config/board_pins.h"
#include "config/profile_defaults.h"
#include "core/time_utils.h"

namespace followbox {
namespace {

// Speed of sound ~343 m/s => 0.343 mm/us; halve for the round trip.
constexpr float kMmPerEchoMicro = 0.1715f;
constexpr uint16_t kTriggerPulseUs = 10;

// Echo capture state shared with the pin-change ISRs. There is exactly one
// UltrasonicArray, so a small file-scope table keeps the ISRs trivial and
// IRAM-safe (no member access, no allocation).
enum EchoIndex { kEchoLeft = 0, kEchoRight = 1, kEchoCount = 2 };

volatile uint32_t g_echo_rise_us[kEchoCount] = {0, 0};
volatile uint32_t g_echo_width_us[kEchoCount] = {0, 0};
volatile bool g_echo_ready[kEchoCount] = {false, false};

template <int Index, int Pin>
void IRAM_ATTR onEchoEdge() {
  if (digitalRead(Pin) == HIGH) {
    g_echo_rise_us[Index] = micros();
  } else {
    g_echo_width_us[Index] = micros() - g_echo_rise_us[Index];
    g_echo_ready[Index] = true;
  }
}

bool consumeEcho(int index, uint32_t* width_us) {
  noInterrupts();
  const bool ready = g_echo_ready[index];
  const uint32_t width = g_echo_width_us[index];
  g_echo_ready[index] = false;
  interrupts();
  if (ready) {
    *width_us = width;
  }
  return ready;
}

}  // namespace

int UltrasonicArray::echoMicrosToMillimeters(uint32_t width_us) {
  if (width_us == 0 || width_us > profile::ULTRASONIC_ECHO_TIMEOUT_US) {
    return -1;
  }
  const int mm = static_cast<int>(static_cast<float>(width_us) * kMmPerEchoMicro);
  if (mm < profile::ULTRASONIC_MIN_VALID_MM ||
      mm > profile::ULTRASONIC_MAX_VALID_MM) {
    return -1;
  }
  return mm;
}

void UltrasonicArray::begin() {
  snapshot_ = UltrasonicSnapshot{};
  stats_ = UltrasonicStats{};
  last_trigger_ms_ = 0;
  triggered_ = false;

  if (pins::PIN_US_SHARED_TRIG >= 0) {
    pinMode(pins::PIN_US_SHARED_TRIG, OUTPUT);
    digitalWrite(pins::PIN_US_SHARED_TRIG, LOW);
  }
  if (pins::PIN_US_LEFT_ECHO >= 0) {
    pinMode(pins::PIN_US_LEFT_ECHO, INPUT);
    attachInterrupt(digitalPinToInterrupt(pins::PIN_US_LEFT_ECHO),
                    onEchoEdge<kEchoLeft, pins::PIN_US_LEFT_ECHO>, CHANGE);
  }
  if (pins::PIN_US_RIGHT_ECHO >= 0) {
    pinMode(pins::PIN_US_RIGHT_ECHO, INPUT);
    attachInterrupt(digitalPinToInterrupt(pins::PIN_US_RIGHT_ECHO),
                    onEchoEdge<kEchoRight, pins::PIN_US_RIGHT_ECHO>, CHANGE);
  }
}

void UltrasonicArray::update(uint32_t now_ms) {
  // Fold any completed echoes captured since the last update.
  uint32_t width = 0;
  if (consumeEcho(kEchoLeft, &width)) {
    const int mm = echoMicrosToMillimeters(width);
    stats_.left_echo_count++;
    snapshot_.left_valid = mm > 0;
    if (mm > 0) {
      snapshot_.left_mm = mm;
    } else {
      stats_.left_timeout_count++;
    }
    snapshot_.last_update_ms = now_ms;
  }
  if (consumeEcho(kEchoRight, &width)) {
    const int mm = echoMicrosToMillimeters(width);
    stats_.right_echo_count++;
    snapshot_.right_valid = mm > 0;
    if (mm > 0) {
      snapshot_.right_mm = mm;
    } else {
      stats_.right_timeout_count++;
    }
    snapshot_.last_update_ms = now_ms;
  }

  // Invalidate readings that stopped arriving so a dead echo never reads "clear".
  if (isStale(now_ms, snapshot_.last_update_ms,
              profile::ULTRASONIC_STALE_TIMEOUT_MS)) {
    snapshot_.left_valid = false;
    snapshot_.right_valid = false;
  }
  snapshot_.valid = snapshot_.left_valid || snapshot_.right_valid;

  // Start the next ping when the cycle window has elapsed.
  if (last_trigger_ms_ == 0 ||
      elapsedMs(now_ms, last_trigger_ms_) >= profile::ULTRASONIC_CYCLE_PERIOD_MS) {
    fireTrigger(now_ms);
  }
}

void UltrasonicArray::fireTrigger(uint32_t now_ms) {
  if (pins::PIN_US_SHARED_TRIG < 0) {
    return;
  }
  digitalWrite(pins::PIN_US_SHARED_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(pins::PIN_US_SHARED_TRIG, HIGH);
  delayMicroseconds(kTriggerPulseUs);
  digitalWrite(pins::PIN_US_SHARED_TRIG, LOW);

  last_trigger_ms_ = now_ms;
  triggered_ = true;
  stats_.trigger_count++;
  stats_.last_cycle_ms = now_ms;
}

}  // namespace followbox
