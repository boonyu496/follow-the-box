#pragma once

#include <cstddef>
#include <cstdint>

namespace followbox {

class LidarEaiS2;
class UartBus;

// Owns the fitted lidar's bring-up shell: startup command, wire/baud probing,
// raw preview diagnostics, and throttled health logs. The parser remains in
// LidarEaiS2; this helper only manages UART-level diagnosis and retries.
class LidarBringupProbe {
 public:
  LidarBringupProbe(UartBus& uart, LidarEaiS2& lidar);

  void logBeginOk();
  void logBeginFailed() const;
  void logDiagnostics(uint32_t now_ms);

 private:
  void clearInput();
  size_t sendStartupSequence();
  void restartCandidate(uint8_t candidate_index, uint32_t now_ms,
                        const char* reason);
  void probeNextCandidate(uint32_t now_ms, const char* reason);

  UartBus& uart_;
  LidarEaiS2& lidar_;
  uint32_t last_diag_ms_ = 0;
  uint32_t last_rx_bytes_ = 0;
  uint32_t last_packets_ = 0;
  uint32_t last_scans_ = 0;
  uint32_t last_checksum_errors_ = 0;
  uint32_t last_framing_errors_ = 0;
  uint32_t last_start_retry_ms_ = 0;
  uint32_t last_raw_diag_ms_ = 0;
  uint32_t current_baud_ = 0;
  int active_rx_pin_ = -1;
  int active_tx_pin_ = -1;
  const char* active_wiring_label_ = nullptr;
  uint8_t probe_index_ = 0;
  uint8_t probe_rounds_ = 0;
  bool healthy_logged_ = false;
};

}  // namespace followbox
