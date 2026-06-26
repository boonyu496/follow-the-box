#pragma once

#include <cstddef>
#include <cstdint>

#include <Arduino.h>

#include "core/system_state.h"

namespace followbox {

class CloudClient {
 public:
  void begin();
  void update(const SystemState& state, uint32_t now_ms);
  CloudControlInput pollInput(uint32_t now_ms);

 private:
  uint32_t telemetryRetryIntervalMs() const;
  bool videoMayRun(uint32_t now_ms) const;
  void pollCommand(uint32_t now_ms);
  bool uploadTelemetry(const SystemState& state, uint32_t now_ms);
  bool uploadCameraFrame(uint32_t now_ms);
  bool applyCommandBody(const char* body, size_t length, uint32_t now_ms);
  void stopMotion();
  void markDisconnected();
  bool configured() const;

  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  CloudControlInput input_;
  uint32_t last_upload_ms_ = 0;
  uint32_t last_upload_success_ms_ = 0;
  uint32_t last_video_upload_ms_ = 0;
  uint32_t last_poll_ms_ = 0;
  uint32_t upload_seq_ = 0;
  uint8_t telemetry_failure_count_ = 0;
  uint32_t video_retry_interval_ms_ = 0;
};

}  // namespace followbox
