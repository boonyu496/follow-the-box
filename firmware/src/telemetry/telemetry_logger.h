#pragma once

#include <cstdint>

#include "core/system_state.h"

namespace followbox {

// Periodic + event-driven SystemState snapshot logger.
//
// Reads an already-committed SystemState copy and emits one compact, greppable
// line via DebugConsole. Per FIRMWARE-SPEC the log layer must never stall the
// control loop, so this runs from the low-rate comm task (Core 0) only and does
// nothing but format a copy of state - no GPIO, no locks, no motion path.
//
// Output is rate-limited to `period_ms` but also fires immediately whenever the
// run mode or stop reason changes, so safety-relevant transitions are never
// hidden between periodic samples.
class TelemetryLogger {
 public:
  void begin(uint32_t period_ms = 1000);

  // Called once per comm-task cycle with the latest committed state. Emits a
  // line when the period elapses or a mode/stop-reason transition is detected.
  void update(const SystemState& state, uint32_t now_ms);

 private:
  void emit(const SystemState& state);

  uint32_t period_ms_ = 1000;
  uint32_t last_emit_ms_ = 0;
  bool primed_ = false;
  RunMode last_mode_ = RunMode::BOOT_SELF_TEST;
  StopReason last_stop_ = StopReason::NONE;
};

}  // namespace followbox
