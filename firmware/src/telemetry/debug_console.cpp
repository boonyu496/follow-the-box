#include "telemetry/debug_console.h"

#include <Arduino.h>

#include <cstdarg>
#include <cstdio>

namespace followbox {
namespace {

// Bounded so a single log line can never allocate or stall the caller. Lines
// longer than this are truncated rather than wrapped.
constexpr size_t kLineBufferSize = 192;
constexpr size_t kRingLines = 12;

const char* levelTag(LogLevel level) {
  switch (level) {
    case LogLevel::kError:
      return "E";
    case LogLevel::kWarn:
      return "W";
    case LogLevel::kInfo:
      return "I";
    case LogLevel::kDebug:
      return "D";
  }
  return "?";
}

size_t appendEscapedJsonString(char* out, size_t out_size, size_t pos,
                               const char* text) {
  if (pos >= out_size) {
    return pos;
  }
  if (pos + 1 >= out_size) {
    return out_size;
  }
  out[pos++] = '"';
  for (const char* p = text; *p != '\0' && pos + 2 < out_size; ++p) {
    const char c = *p;
    if (c == '"' || c == '\\') {
      if (pos + 2 >= out_size) {
        return out_size;
      }
      out[pos++] = '\\';
      out[pos++] = c;
    } else if (c == '\n' || c == '\r') {
      continue;
    } else {
      out[pos++] = c;
    }
  }
  if (pos + 1 >= out_size) {
    return out_size;
  }
  out[pos++] = '"';
  return pos;
}

}  // namespace

LogLevel DebugConsole::level_ = LogLevel::kInfo;
bool DebugConsole::ready_ = false;
namespace {
portMUX_TYPE g_log_mux = portMUX_INITIALIZER_UNLOCKED;
char g_ring[kRingLines][kLineBufferSize];
size_t g_write_idx = 0;
size_t g_count = 0;
}  // namespace

void DebugConsole::begin(LogLevel level) {
  level_ = level;
  ready_ = true;
}

void DebugConsole::log(LogLevel level, const char* fmt, ...) {
  if (!ready_ || !enabled(level)) {
    return;
  }

  char buf[kLineBufferSize];
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  if (n < 0) {
    return;
  }

  // millis() timestamp + level tag give every line a stable, greppable prefix.
  // Serial is the buffered USB-CDC sink (ARDUINO_USB_CDC_ON_BOOT=1); the write
  // returns once bytes are queued and never blocks the control loop.
  char line[kLineBufferSize];
  std::snprintf(line, sizeof(line), "[%lu][%s] %s",
                static_cast<unsigned long>(millis()), levelTag(level), buf);

  portENTER_CRITICAL(&g_log_mux);
  std::snprintf(g_ring[g_write_idx], kLineBufferSize, "%s", line);
  g_write_idx = (g_write_idx + 1) % kRingLines;
  if (g_count < kRingLines) {
    ++g_count;
  }
  portEXIT_CRITICAL(&g_log_mux);

  Serial.printf("%s\n", line);
}

size_t DebugConsole::drainRecentJson(char* out, size_t out_size) {
  if (out == nullptr || out_size < 3) {
    return 0;
  }

  char copy[kRingLines][kLineBufferSize];
  size_t count = 0;
  size_t start = 0;

  portENTER_CRITICAL(&g_log_mux);
  count = g_count;
  start = (g_write_idx + kRingLines - g_count) % kRingLines;
  for (size_t i = 0; i < count; ++i) {
    const size_t idx = (start + i) % kRingLines;
    std::snprintf(copy[i], kLineBufferSize, "%s", g_ring[idx]);
  }
  g_count = 0;
  portEXIT_CRITICAL(&g_log_mux);

  size_t pos = 0;
  out[pos++] = '[';
  for (size_t i = 0; i < count; ++i) {
    if (i > 0) {
      if (pos + 1 >= out_size) {
        out[0] = '\0';
        return 0;
      }
      out[pos++] = ',';
    }
    pos = appendEscapedJsonString(out, out_size, pos, copy[i]);
    if (pos >= out_size) {
      out[0] = '\0';
      return 0;
    }
  }
  if (pos + 1 >= out_size) {
    out[0] = '\0';
    return 0;
  }
  out[pos++] = ']';
  out[pos] = '\0';
  return pos;
}

}  // namespace followbox
