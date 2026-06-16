#pragma once

#include <cstdint>

#include "core/types.h"

namespace followbox {

// Diagnostics for the side ultrasonic pair. Telemetry only.
struct UltrasonicStats {
  uint32_t trigger_count = 0;
  uint32_t left_echo_count = 0;
  uint32_t right_echo_count = 0;
  uint32_t left_timeout_count = 0;
  uint32_t right_timeout_count = 0;
  uint32_t last_cycle_ms = 0;
};

// Side-clearance driver for two HC-SR04 ultrasonic modules that share one TRIG.
//
// Wiring (CURRENT-WIRING-AI.md / PIN-MAP-V1.md): a single shared TRIG on
// PIN_US_SHARED_TRIG (GPIO9) fires both modules at once; each Echo returns on its
// own pin (left = GPIO40, right = GPIO41) through a divider/level shifter to 3.3V.
//
// The cycle is fully non-blocking: update() emits a short trigger pulse no more
// often than ULTRASONIC_CYCLE_PERIOD_MS and reads the echo widths captured by pin
// change interrupts, so the sensor task never busy-waits in pulseIn(). Out-of-
// range or missing echoes keep the matching *_valid flag low instead of faking a
// distance. This is an auxiliary side sensor: it produces a snapshot only and
// never touches the motion path. Mounting side/orientation MUST be confirmed on
// the bench before the readings are trusted.
class UltrasonicArray {
 public:
  void begin();

  // Drive the trigger when due and fold any completed echoes into the snapshot.
  void update(uint32_t now_ms);

  const UltrasonicSnapshot& snapshot() const { return snapshot_; }
  const UltrasonicStats& stats() const { return stats_; }

  // Pure conversion (exposed for testing): echo pulse width -> distance in mm.
  // Returns -1 when the width is outside the HC-SR04 usable range.
  static int echoMicrosToMillimeters(uint32_t width_us);

 private:
  void fireTrigger(uint32_t now_ms);

  UltrasonicSnapshot snapshot_;
  UltrasonicStats stats_;
  uint32_t last_trigger_ms_ = 0;
  bool triggered_ = false;
};

}  // namespace followbox
