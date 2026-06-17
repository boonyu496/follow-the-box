#pragma once

#include <cstddef>
#include <cstdint>

namespace followbox {

// Severity ordering: a message is emitted only when its level is at or below
// the configured threshold (kError is always shown, kDebug is the most verbose).
enum class LogLevel : uint8_t {
  kError = 0,
  kWarn = 1,
  kInfo = 2,
  kDebug = 3,
};

// Leveled, printf-style debug log over the native USB-CDC serial port.
//
// Per FIRMWARE-SPEC the telemetry/log layer must never block the control loop:
// every call formats into a fixed stack buffer (no heap) and hands the bytes to
// the buffered USB-CDC driver, so it is safe to call from any task. It writes no
// GPIO and takes no part in the motion or safety path - it only prints.
//
// All members are static so any module can log without holding an instance.
class DebugConsole {
 public:
  // Capture the serial sink and the initial verbosity. Call once from setup()
  // after Serial.begin(). Safe to call again to change the sink/level.
  static void begin(LogLevel level = LogLevel::kInfo);

  static void setLevel(LogLevel level) { level_ = level; }
  static LogLevel level() { return level_; }

  // Format and emit a single line (a trailing newline is added automatically)
  // when `level` is enabled. Output is truncated to a bounded buffer; the
  // formatting cost is paid by the caller's task, never the control loop.
  static void log(LogLevel level, const char* fmt, ...)
      __attribute__((format(printf, 2, 3)));

  // Drain recent log lines into a JSON string array. Lines are escaped and the
  // ring is cleared after copying. Intended for low-priority cloud telemetry.
  static size_t drainRecentJson(char* out, size_t out_size);

  // Copy recent log lines into a JSON string array without clearing the ring.
  // Intended for local H5 diagnostics so AP/LAN log viewing cannot starve the
  // cloud ingest path.
  static size_t copyRecentJson(char* out, size_t out_size);

 private:
  static bool enabled(LogLevel level) { return level <= level_; }

  static LogLevel level_;
  static bool ready_;
};

}  // namespace followbox

// Convenience macros keep call sites short and let disabled levels skip
// formatting work entirely.
#define FB_LOGE(...) ::followbox::DebugConsole::log(::followbox::LogLevel::kError, __VA_ARGS__)
#define FB_LOGW(...) ::followbox::DebugConsole::log(::followbox::LogLevel::kWarn, __VA_ARGS__)
#define FB_LOGI(...) ::followbox::DebugConsole::log(::followbox::LogLevel::kInfo, __VA_ARGS__)
#define FB_LOGD(...) ::followbox::DebugConsole::log(::followbox::LogLevel::kDebug, __VA_ARGS__)
