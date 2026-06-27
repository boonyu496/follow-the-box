#include "sensors/sensor_task.h"

#include <algorithm>
#include <cstddef>

#include <Arduino.h>

#include "config/board_pins.h"
#include "config/profile_defaults.h"
#include "core/time_utils.h"
#include "telemetry/debug_console.h"

namespace followbox {

namespace {
constexpr uint8_t kLidarStartCommand[] = {0xA5, 0x60};
constexpr size_t kLidarStartupSequenceBytes = sizeof(kLidarStartCommand);
constexpr uint32_t kLidarStartupGraceMs = 3000;
constexpr uint32_t kLidarDiagPeriodMs = 5000;
constexpr uint32_t kLidarRawDiagPeriodMs = 30000;
constexpr uint32_t kLidarRestartRetryMs = 15000;
constexpr uint32_t kLidarLineSampleUs = 2000;
constexpr uint8_t kLidarMaxProbeRounds = 2;
constexpr uint32_t kLidarProbeBauds[] = {115200, 150000, 230400, 128000, 256000};
constexpr size_t kLidarProbeBaudCount =
  sizeof(kLidarProbeBauds) / sizeof(kLidarProbeBauds[0]);

struct LidarWiringCandidate {
  int rx_pin;
  int tx_pin;
  const char* label;
};

constexpr LidarWiringCandidate kLidarWirings[] = {
    {pins::PIN_LIDAR_RX, pins::PIN_LIDAR_TX, "spec(DATA/TX->RX CTL/RX<-TX)"},
    {pins::PIN_LIDAR_TX, pins::PIN_LIDAR_RX, "swap(CTL/TX->RX DATA/RX<-TX)"},
};
constexpr size_t kLidarWiringCount =
    sizeof(kLidarWirings) / sizeof(kLidarWirings[0]);
constexpr size_t kLidarProbeCandidateCount =
    kLidarProbeBaudCount * kLidarWiringCount;

struct LidarLineSample {
  int final_level = -1;
  uint32_t high_count = 0;
  uint32_t low_count = 0;
  uint32_t transitions = 0;
};

uint16_t readLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(p[1]) << 8);
}

uint16_t readBe16(const uint8_t* p) {
  return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) << 8) |
         static_cast<uint16_t>(p[1]);
}

float s2AngleDeg(uint16_t encoded) {
  return static_cast<float>(encoded >> 1) / 64.0f;
}

void bytesToHex(const uint8_t* bytes, size_t count, char* out,
                size_t out_size) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  if (out == nullptr || out_size == 0) {
    return;
  }
  const size_t max_bytes = (out_size - 1) / 2;
  const size_t emit = count < max_bytes ? count : max_bytes;
  for (size_t i = 0; i < emit; ++i) {
    out[i * 2] = kHex[bytes[i] >> 4];
    out[i * 2 + 1] = kHex[bytes[i] & 0x0Fu];
  }
  out[emit * 2] = '\0';
}

LidarLineSample sampleLineLevel(int pin) {
  LidarLineSample sample;
  if (pin < 0) {
    return sample;
  }

  const uint32_t start_us = micros();
  int previous = digitalRead(pin);
  sample.final_level = previous;
  while (static_cast<uint32_t>(micros() - start_us) < kLidarLineSampleUs) {
    const int level = digitalRead(pin);
    if (level == HIGH) {
      ++sample.high_count;
    } else {
      ++sample.low_count;
    }
    if (previous != -1 && level != previous) {
      ++sample.transitions;
    }
    previous = level;
    sample.final_level = level;
  }
  return sample;
}

void logHeader55aaCandidate(const uint8_t* bytes, size_t size) {
  if (bytes == nullptr || size < 8 || bytes[0] != 0x55 || bytes[1] != 0xAA) {
    return;
  }

  const uint8_t count = bytes[3];
  if (count == 0 || count > 80) {
    FB_LOGW("LIDAR raw 55aa cand bad_count ct=0x%02X count=%u size=%u",
            bytes[2], static_cast<unsigned>(count),
            static_cast<unsigned>(size));
    return;
  }

  const size_t no_checksum_length = 8u + static_cast<size_t>(count) * 3u;
  if (size < no_checksum_length) {
    FB_LOGW(
        "LIDAR raw 55aa cand short ct=0x%02X count=%u size=%u need=%u",
        bytes[2], static_cast<unsigned>(count), static_cast<unsigned>(size),
        static_cast<unsigned>(no_checksum_length));
    return;
  }

  uint16_t qfirst_min_mm4 = 0xFFFFu;
  uint16_t qfirst_max_mm4 = 0;
  uint16_t dfirst_min_raw = 0xFFFFu;
  uint16_t dfirst_max_raw = 0;
  uint16_t dfirst_min_mm4 = 0xFFFFu;
  uint16_t dfirst_max_mm4 = 0;
  uint8_t dfirst_min_quality = 0xFFu;
  uint8_t dfirst_max_quality = 0;
  for (uint8_t i = 0; i < count; ++i) {
    const size_t sample_offset = 8u + static_cast<size_t>(i) * 3u;
    const uint16_t qfirst_raw = readLe16(bytes + sample_offset + 1u);
    const uint16_t dfirst_raw = readLe16(bytes + sample_offset);
    const uint16_t qfirst_mm4 = static_cast<uint16_t>(qfirst_raw / 4u);
    const uint16_t dfirst_mm4 = static_cast<uint16_t>(dfirst_raw / 4u);
    const uint8_t dfirst_quality = bytes[sample_offset + 2u];
    qfirst_min_mm4 = std::min(qfirst_min_mm4, qfirst_mm4);
    qfirst_max_mm4 = std::max(qfirst_max_mm4, qfirst_mm4);
    dfirst_min_raw = std::min(dfirst_min_raw, dfirst_raw);
    dfirst_max_raw = std::max(dfirst_max_raw, dfirst_raw);
    dfirst_min_mm4 = std::min(dfirst_min_mm4, dfirst_mm4);
    dfirst_max_mm4 = std::max(dfirst_max_mm4, dfirst_mm4);
    dfirst_min_quality = std::min(dfirst_min_quality, dfirst_quality);
    dfirst_max_quality = std::max(dfirst_max_quality, dfirst_quality);
  }

  char trailer_hex[9];
  bytesToHex(bytes + no_checksum_length,
             size > no_checksum_length ? size - no_checksum_length : 0,
             trailer_hex, sizeof(trailer_hex));
  FB_LOGW(
      "LIDAR raw 55aa cand ct=0x%02X count=%u len_no_cs=%u "
      "angle_le=%.1f..%.1f angle_be=%.1f..%.1f "
      "qfirst_mm4=%u..%u dfirst_raw=%u..%u dfirst_mm4=%u..%u "
      "dfirst_q=%u..%u trailer=%s",
      bytes[2], static_cast<unsigned>(count),
      static_cast<unsigned>(no_checksum_length),
      static_cast<double>(s2AngleDeg(readLe16(bytes + 4u))),
      static_cast<double>(s2AngleDeg(readLe16(bytes + 6u))),
      static_cast<double>(s2AngleDeg(readBe16(bytes + 4u))),
      static_cast<double>(s2AngleDeg(readBe16(bytes + 6u))),
      static_cast<unsigned>(qfirst_min_mm4),
      static_cast<unsigned>(qfirst_max_mm4),
      static_cast<unsigned>(dfirst_min_raw),
      static_cast<unsigned>(dfirst_max_raw),
      static_cast<unsigned>(dfirst_min_mm4),
      static_cast<unsigned>(dfirst_max_mm4),
      static_cast<unsigned>(dfirst_min_quality),
      static_cast<unsigned>(dfirst_max_quality), trailer_hex);
}
}  // namespace

SensorTask::SensorTask()
    : uwb_uart_(pins::UART_NUM_UWB, pins::PIN_UWB_RX, pins::PIN_UWB_TX,
                profile::UWB_UART_BAUD),
      lidar_uart_(pins::UART_NUM_LIDAR, pins::PIN_LIDAR_RX, pins::PIN_LIDAR_TX,
                  profile::LIDAR_UART_BAUD),
      imu_uart_(pins::UART_NUM_IMU, pins::PIN_IMU_RX, pins::PIN_IMU_TX,
                profile::IMU_UART_BAUD),
      estop_status_(pins::PIN_ESTOP_STATUS, ActiveLevel::ACTIVE_HIGH,
                    InputPull::PULL_UP),
      lidar_active_rx_pin_(pins::PIN_LIDAR_RX),
      lidar_active_tx_pin_(pins::PIN_LIDAR_TX),
      lidar_active_wiring_label_(kLidarWirings[0].label) {}

void SensorTask::begin() {
  uwb_parser_.reset();
  lidar_.reset();
  imu_.reset();
  camera_.reset();
  estop_status_.begin();
  estop_active_ = true;
  uwb_uart_.begin();
  lidar_current_baud_ = profile::LIDAR_UART_BAUD;
  if (lidar_uart_.begin()) {
    const size_t written = sendLidarStartupSequence();
    FB_LOGI(
        "LIDAR begin: uart=%d wiring=%s rx=GPIO%d tx=GPIO%d baud=%lu "
        "startup=A560 auto_probe=wire+baud "
        "written=%u",
        pins::UART_NUM_LIDAR,
        lidar_active_wiring_label_ != nullptr ? lidar_active_wiring_label_ : "?",
        lidar_active_rx_pin_, lidar_active_tx_pin_,
        static_cast<unsigned long>(lidar_current_baud_),
        static_cast<unsigned>(written));
    if (written != kLidarStartupSequenceBytes) {
      FB_LOGW("LIDAR begin start_write_short expected=%u written=%u",
              static_cast<unsigned>(kLidarStartupSequenceBytes),
              static_cast<unsigned>(written));
    }
  } else if (lidar_uart_.isEnabled()) {
    FB_LOGW("LIDAR begin failed: uart=%d rx=GPIO%d tx=GPIO%d baud=%lu",
            pins::UART_NUM_LIDAR, lidar_active_rx_pin_, lidar_active_tx_pin_,
            static_cast<unsigned long>(profile::LIDAR_UART_BAUD));
  }
  if (imu_uart_.begin()) {
    FB_LOGI("IMU begin: uart=%d rx=GPIO%d tx=%s baud=%lu",
            pins::UART_NUM_IMU, pins::PIN_IMU_RX,
            pins::PIN_IMU_TX >= 0 ? "enabled" : "disabled",
            static_cast<unsigned long>(profile::IMU_UART_BAUD));
  } else if (imu_uart_.isEnabled()) {
    FB_LOGW("IMU begin failed: uart=%d rx=GPIO%d tx=%s baud=%lu",
            pins::UART_NUM_IMU, pins::PIN_IMU_RX,
            pins::PIN_IMU_TX >= 0 ? "enabled" : "disabled",
            static_cast<unsigned long>(profile::IMU_UART_BAUD));
  }
  power_monitor_.begin();
  tof_.begin();
  ultrasonic_.begin();
}

void SensorTask::update(uint32_t now_ms) {
  const uint32_t update_start_ms = millis();
  drainUwb(now_ms);
  drainLidar(now_ms);
  drainImu(now_ms);
  estop_active_ = !estop_status_.isValid() || estop_status_.readActive();
  power_monitor_.update(now_ms);
  tof_.update(now_ms);
  ultrasonic_.update(now_ms);
  camera_.update(now_ms);

  // Apply staleness timeouts even when no new bytes arrived so the snapshots
  // invalidate when a stream stops instead of latching stale headings.
  uwb_parser_.update(now_ms);
  lidar_.update(now_ms);
  logLidarDiagnostics(now_ms);
  imu_.update(now_ms);

  // Fuse the lidar sweep, forward TOF array and side ultrasonic into the single
  // ObstacleSnapshot the motion path reads (closest valid reading per sector).
  fused_obstacle_ =
      fuseObstacles(lidar_.snapshot(), tof_.snapshot(), ultrasonic_.snapshot());

  // Heartbeats: the loop completed, so the safety watchdog stays satisfied.
  uwb_heartbeat_ms_ = now_ms;
  sensor_heartbeat_ms_ = now_ms;

  const uint32_t update_elapsed_ms = elapsedMs(millis(), update_start_ms);
  if (update_elapsed_ms > profile::TASK_HEARTBEAT_TIMEOUT_MS / 2 &&
      (last_slow_update_log_ms_ == 0 ||
       elapsedMs(now_ms, last_slow_update_log_ms_) >= kLidarDiagPeriodMs)) {
    last_slow_update_log_ms_ = now_ms;
    FB_LOGW("sensor_task slow update dt=%lums lidar_rx=%lu pkt=%lu tof_read=%lu",
            static_cast<unsigned long>(update_elapsed_ms),
            static_cast<unsigned long>(lidar_.stats().rx_byte_count),
            static_cast<unsigned long>(lidar_.stats().packet_count),
            static_cast<unsigned long>(tof_.stats().read_count));
  }
}

void SensorTask::drainUwb(uint32_t now_ms) {
  int budget = profile::SENSOR_TASK_MAX_BYTES_PER_UPDATE;
  while (budget-- > 0 && uwb_uart_.available() > 0) {
    const int byte = uwb_uart_.read();
    if (byte < 0) {
      break;
    }
    uwb_parser_.pushByte(static_cast<uint8_t>(byte), now_ms);
  }
}

void SensorTask::drainLidar(uint32_t now_ms) {
  if (!lidar_uart_.isEnabled()) {
    return;
  }
  int budget = profile::SENSOR_TASK_MAX_BYTES_PER_UPDATE;
  while (budget-- > 0 && lidar_uart_.available() > 0) {
    const int byte = lidar_uart_.read();
    if (byte < 0) {
      break;
    }
    lidar_.pushByte(static_cast<uint8_t>(byte), now_ms);
  }
}

void SensorTask::drainImu(uint32_t now_ms) {
  if (!imu_uart_.isEnabled()) {
    return;
  }
  int budget = profile::SENSOR_TASK_MAX_BYTES_PER_UPDATE;
  while (budget-- > 0 && imu_uart_.available() > 0) {
    const int byte = imu_uart_.read();
    if (byte < 0) {
      break;
    }
    imu_.pushByte(static_cast<uint8_t>(byte), now_ms);
  }
}

void SensorTask::restartLidarCandidate(uint8_t candidate_index, uint32_t now_ms,
                                       const char* reason) {
  if (!lidar_uart_.isEnabled()) {
    return;
  }

  if (candidate_index >= kLidarProbeCandidateCount) {
    candidate_index = 0;
  }
  const size_t baud_index =
      static_cast<size_t>(candidate_index) / kLidarWiringCount;
  const size_t wiring_index =
      static_cast<size_t>(candidate_index) % kLidarWiringCount;
  const uint32_t baud = kLidarProbeBauds[baud_index];
  const LidarWiringCandidate& wiring = kLidarWirings[wiring_index];

  lidar_.reset();
  last_lidar_rx_bytes_ = 0;
  last_lidar_packets_ = 0;
  last_lidar_scans_ = 0;
  last_lidar_checksum_errors_ = 0;
  last_lidar_framing_errors_ = 0;
  lidar_healthy_logged_ = false;
  if (!lidar_uart_.restart(baud, wiring.rx_pin, wiring.tx_pin)) {
    FB_LOGW("LIDAR probe restart failed candidate=%u baud=%lu wiring=%s "
            "rx=GPIO%d tx=GPIO%d reason=%s",
            static_cast<unsigned>(candidate_index),
            static_cast<unsigned long>(baud), wiring.label, wiring.rx_pin,
            wiring.tx_pin, reason != nullptr ? reason : "?");
    return;
  }

  lidar_probe_index_ = candidate_index;
  lidar_current_baud_ = baud;
  lidar_active_rx_pin_ = wiring.rx_pin;
  lidar_active_tx_pin_ = wiring.tx_pin;
  lidar_active_wiring_label_ = wiring.label;
  const size_t written = sendLidarStartupSequence();
  FB_LOGW(
      "LIDAR probe candidate=%u/%u round=%u/%u wiring=%s "
      "rx=GPIO%d tx=GPIO%d baud=%lu reason=%s startup=A560 written=%u t=%lu",
      static_cast<unsigned>(lidar_probe_index_),
      static_cast<unsigned>(kLidarProbeCandidateCount - 1u),
      static_cast<unsigned>(lidar_probe_rounds_),
      static_cast<unsigned>(kLidarMaxProbeRounds),
      lidar_active_wiring_label_ != nullptr ? lidar_active_wiring_label_ : "?",
      lidar_active_rx_pin_, lidar_active_tx_pin_,
      static_cast<unsigned long>(lidar_current_baud_),
      reason != nullptr ? reason : "?", static_cast<unsigned>(written),
      static_cast<unsigned long>(now_ms));
}

void SensorTask::probeNextLidarCandidate(uint32_t now_ms, const char* reason) {
  if (lidar_probe_rounds_ >= kLidarMaxProbeRounds) {
    if (lidar_probe_index_ != 0) {
      restartLidarCandidate(0, now_ms, "probe_exhausted_return_primary");
    }
    return;
  }

  uint8_t next =
      static_cast<uint8_t>((lidar_probe_index_ + 1u) % kLidarProbeCandidateCount);
  if (next == 0) {
    ++lidar_probe_rounds_;
  }
  restartLidarCandidate(next, now_ms, reason);
}

void SensorTask::clearLidarInput() {
  int budget = profile::SENSOR_TASK_MAX_BYTES_PER_UPDATE;
  while (budget-- > 0 && lidar_uart_.available() > 0) {
    if (lidar_uart_.read() < 0) {
      break;
    }
  }
}

size_t SensorTask::sendLidarStartupSequence() {
  // Match the locally captured EaiLidarTest working path exactly: it logs only
  // A560 before receiving AA55 scan packets. Avoid extra stop commands while
  // the S2-YJ startup requirements are still being isolated.
  clearLidarInput();
  return lidar_uart_.write(kLidarStartCommand, sizeof(kLidarStartCommand));
}

void SensorTask::logLidarDiagnostics(uint32_t now_ms) {
  if (!lidar_uart_.isEnabled()) {
    return;
  }

  if (last_lidar_diag_ms_ != 0 &&
      elapsedMs(now_ms, last_lidar_diag_ms_) < kLidarDiagPeriodMs) {
    return;
  }
  last_lidar_diag_ms_ = now_ms;

  const LidarS2Stats& stats = lidar_.stats();
  const ObstacleSnapshot& lidar = lidar_.snapshot();
  const uint32_t delta_rx = stats.rx_byte_count - last_lidar_rx_bytes_;
  const uint32_t delta_packets = stats.packet_count - last_lidar_packets_;
  const uint32_t delta_scans = stats.scan_count - last_lidar_scans_;
  const uint32_t delta_checksum_errors =
      stats.checksum_error_count - last_lidar_checksum_errors_;
  const uint32_t delta_framing_errors =
      stats.framing_error_count - last_lidar_framing_errors_;
  const bool lidar_stalled_without_packet =
      stats.packet_count == 0 && stats.rx_byte_count > 0 && delta_rx == 0;
  const bool log_line_evidence =
      now_ms >= kLidarStartupGraceMs && stats.packet_count == 0;
  const LidarLineSample rx_line =
      log_line_evidence ? sampleLineLevel(lidar_active_rx_pin_) : LidarLineSample{};
  const LidarLineSample tx_line =
      log_line_evidence ? sampleLineLevel(lidar_active_tx_pin_) : LidarLineSample{};
  const uint32_t last_rx_age_ms =
      stats.last_rx_ms == 0 ? 0 : elapsedMs(now_ms, stats.last_rx_ms);

  if (now_ms >= kLidarStartupGraceMs) {
    if (stats.rx_byte_count == 0) {
      FB_LOGW(
          "LIDAR diag no_rx rx=0 packets=0 scans=0 baud=%lu "
          "round=%u/%u candidate=%u/%u wiring=%s "
          "rx_line=%d h/l/t=%lu/%lu/%lu tx_line=%d h/l/t=%lu/%lu/%lu "
          "check DATA/TX->GPIO3, CTL/RX<-GPIO43, 5V/GND, common ground, "
          "lidar motor/start, and whether the active RX line idles high",
          static_cast<unsigned long>(lidar_current_baud_),
          static_cast<unsigned>(lidar_probe_rounds_),
          static_cast<unsigned>(kLidarMaxProbeRounds),
          static_cast<unsigned>(lidar_probe_index_),
          static_cast<unsigned>(kLidarProbeCandidateCount - 1u),
          lidar_active_wiring_label_ != nullptr ? lidar_active_wiring_label_ : "?",
          rx_line.final_level,
          static_cast<unsigned long>(rx_line.high_count),
          static_cast<unsigned long>(rx_line.low_count),
          static_cast<unsigned long>(rx_line.transitions),
          tx_line.final_level,
          static_cast<unsigned long>(tx_line.high_count),
          static_cast<unsigned long>(tx_line.low_count),
          static_cast<unsigned long>(tx_line.transitions));
      if (last_lidar_start_retry_ms_ == 0 ||
          elapsedMs(now_ms, last_lidar_start_retry_ms_) >= kLidarRestartRetryMs) {
        last_lidar_start_retry_ms_ = now_ms;
        probeNextLidarCandidate(now_ms, "no_rx_try_next_wire_or_baud");
      }
    } else if (stats.packet_count == 0) {
      const bool emit_raw_diag =
          last_lidar_raw_diag_ms_ == 0 ||
          elapsedMs(now_ms, last_lidar_raw_diag_ms_) >= kLidarRawDiagPeriodMs;
      const bool should_probe_stalled =
          lidar_stalled_without_packet &&
          (last_lidar_start_retry_ms_ == 0 ||
           elapsedMs(now_ms, last_lidar_start_retry_ms_) >= kLidarRestartRetryMs);
      if (should_probe_stalled) {
        FB_LOGW("LIDAR rx_stalled next_probe rx=%lu first_byte_seen_no_stream",
                static_cast<unsigned long>(stats.rx_byte_count));
      }
      FB_LOGW(
          "LIDAR diag rx_no_packets rx=%lu(+%lu) aa55=%lu 55aa=%lu ld54=%lu "
          "framing=%lu(+%lu) checksum=%lu(+%lu) baud=%lu "
          "round=%u/%u candidate=%u/%u "
          "wiring=%s rx_pin=%d tx_pin=%d last_rx_age=%lums "
          "rx_line=%d h/l/t=%lu/%lu/%lu "
          "tx_line=%d h/l/t=%lu/%lu/%lu",
          static_cast<unsigned long>(stats.rx_byte_count),
          static_cast<unsigned long>(delta_rx),
          static_cast<unsigned long>(stats.aa55_header_count),
          static_cast<unsigned long>(stats.header_55aa_count),
          static_cast<unsigned long>(stats.ld_header_count),
          static_cast<unsigned long>(stats.framing_error_count),
          static_cast<unsigned long>(delta_framing_errors),
          static_cast<unsigned long>(stats.checksum_error_count),
          static_cast<unsigned long>(delta_checksum_errors),
          static_cast<unsigned long>(lidar_current_baud_),
          static_cast<unsigned>(lidar_probe_rounds_),
          static_cast<unsigned>(kLidarMaxProbeRounds),
          static_cast<unsigned>(lidar_probe_index_),
          static_cast<unsigned>(kLidarProbeCandidateCount - 1u),
          lidar_active_wiring_label_ != nullptr ? lidar_active_wiring_label_ : "?",
          lidar_active_rx_pin_, lidar_active_tx_pin_,
          static_cast<unsigned long>(last_rx_age_ms),
          rx_line.final_level,
          static_cast<unsigned long>(rx_line.high_count),
          static_cast<unsigned long>(rx_line.low_count),
          static_cast<unsigned long>(rx_line.transitions),
          tx_line.final_level,
          static_cast<unsigned long>(tx_line.high_count),
          static_cast<unsigned long>(tx_line.low_count),
          static_cast<unsigned long>(tx_line.transitions));
      if (emit_raw_diag) {
        last_lidar_raw_diag_ms_ = now_ms;
        char raw_hex[97];
        bytesToHex(lidar_.rawPreview(), lidar_.rawPreviewSize(), raw_hex,
                   sizeof(raw_hex));
        FB_LOGW(
            "LIDAR raw first=%s rejects=count:%lu/fsa:%lu/lsa:%lu/ovf:%lu",
            raw_hex,
            static_cast<unsigned long>(stats.invalid_count_reject_count),
            static_cast<unsigned long>(stats.first_angle_reject_count),
            static_cast<unsigned long>(stats.last_angle_reject_count),
            static_cast<unsigned long>(stats.overflow_reject_count));
        if (lidar_.aa55PreviewSize() > 0) {
          char aa55_hex[97];
          bytesToHex(lidar_.aa55Preview(), lidar_.aa55PreviewSize(), aa55_hex,
                     sizeof(aa55_hex));
          FB_LOGW("LIDAR raw aa55=%s", aa55_hex);
        }
        if (lidar_.header55aaPreviewSize() > 0) {
          char header55aa_hex[97];
          bytesToHex(lidar_.header55aaPreview(),
                     lidar_.header55aaPreviewSize(), header55aa_hex,
                     sizeof(header55aa_hex));
          FB_LOGW("LIDAR raw 55aa=%s", header55aa_hex);
          logHeader55aaCandidate(lidar_.header55aaPreview(),
                                 lidar_.header55aaPreviewSize());
        }
      }
      if (delta_rx > 0 && stats.packet_count == 0) {
        probeNextLidarCandidate(now_ms, "rx_without_known_header");
      } else if (should_probe_stalled) {
        last_lidar_start_retry_ms_ = now_ms;
        probeNextLidarCandidate(now_ms, "rx_stalled_one_byte");
      }
    } else if (stats.scan_count == 0) {
      FB_LOGW(
          "LIDAR diag packets_no_scan rx=%lu packets=%lu(+%lu) scans=0 "
          "no_cs=%lu no_i=%lu last_packet=%lu "
          "check AA55 intensity8/NODE_QUAL0 frames and start command A560",
          static_cast<unsigned long>(stats.rx_byte_count),
          static_cast<unsigned long>(stats.packet_count),
          static_cast<unsigned long>(delta_packets),
          static_cast<unsigned long>(stats.no_checksum_packet_count),
          static_cast<unsigned long>(stats.no_intensity_packet_count),
          static_cast<unsigned long>(stats.last_packet_ms));
    } else if (!lidar.valid) {
      FB_LOGW(
          "LIDAR diag stale valid=0 rx=%lu packets=%lu scans=%lu(+%lu) "
          "last_packet=%lu timeout=%lu check intermittent power/connector",
          static_cast<unsigned long>(stats.rx_byte_count),
          static_cast<unsigned long>(stats.packet_count),
          static_cast<unsigned long>(stats.scan_count),
          static_cast<unsigned long>(delta_scans),
          static_cast<unsigned long>(stats.last_packet_ms),
          static_cast<unsigned long>(profile::LIDAR_PACKET_TIMEOUT_MS));
    } else if (delta_checksum_errors > 0 || delta_framing_errors > 0) {
      FB_LOGW(
          "LIDAR diag parse_errors rx=%lu packets=%lu scans=%lu "
          "checksum=%lu(+%lu) framing=%lu(+%lu) check baud/noise/ground",
          static_cast<unsigned long>(stats.rx_byte_count),
          static_cast<unsigned long>(stats.packet_count),
          static_cast<unsigned long>(stats.scan_count),
          static_cast<unsigned long>(stats.checksum_error_count),
          static_cast<unsigned long>(delta_checksum_errors),
          static_cast<unsigned long>(stats.framing_error_count),
          static_cast<unsigned long>(delta_framing_errors));
    } else if (!lidar_healthy_logged_) {
      FB_LOGI(
          "LIDAR diag ok rx=%lu packets=%lu scans=%lu no_cs=%lu no_i=%lu 55aa_pkt=%lu "
          "sectors=%d/%d/%d/%d/%d",
          static_cast<unsigned long>(stats.rx_byte_count),
          static_cast<unsigned long>(stats.packet_count),
          static_cast<unsigned long>(stats.scan_count),
          static_cast<unsigned long>(stats.no_checksum_packet_count),
          static_cast<unsigned long>(stats.no_intensity_packet_count),
          static_cast<unsigned long>(stats.header_55aa_packet_count),
          lidar.front_left_mm,
          lidar.front_center_mm, lidar.front_right_mm, lidar.side_left_mm,
          lidar.side_right_mm);
      lidar_healthy_logged_ = true;
    }
  }

  last_lidar_rx_bytes_ = stats.rx_byte_count;
  last_lidar_packets_ = stats.packet_count;
  last_lidar_scans_ = stats.scan_count;
  last_lidar_checksum_errors_ = stats.checksum_error_count;
  last_lidar_framing_errors_ = stats.framing_error_count;
}

SensorDiagnostics SensorTask::diagnostics() const {
  SensorDiagnostics out;
  const ObstacleSnapshot& lidar = lidar_.snapshot();
  const LidarS2Stats& lidar_stats = lidar_.stats();
  const TofStats& tof_stats = tof_.stats();
  out.lidar_valid = lidar.valid;
  out.lidar_last_update_ms = lidar.last_update_ms;
  out.lidar_front_left_mm = lidar.front_left_mm;
  out.lidar_front_center_mm = lidar.front_center_mm;
  out.lidar_front_right_mm = lidar.front_right_mm;
  out.lidar_side_left_mm = lidar.side_left_mm;
  out.lidar_side_right_mm = lidar.side_right_mm;
  out.lidar_rx_bytes = lidar_stats.rx_byte_count;
  out.lidar_packets = lidar_stats.packet_count;
  out.lidar_checksum_errors = lidar_stats.checksum_error_count;
  out.lidar_framing_errors = lidar_stats.framing_error_count;
  out.lidar_scans = lidar_stats.scan_count;
  out.tof_init_ok_mask = tof_stats.init_ok_mask;
  out.tof_init_attempt_count = tof_stats.init_attempt_count;
  out.tof_init_failure_count = tof_stats.init_failure_count;
  out.tof_read_count = tof_stats.read_count;
  out.tof_timeout_count = tof_stats.timeout_count;
  out.tof_mux_nack_count = tof_stats.mux_nack_count;
  out.tof_bus_clear_count = tof_stats.bus_clear_count;
  out.tof_reinit_count = tof_stats.reinit_count;
  out.tof_last_recovery_ms = tof_stats.last_recovery_ms;
  return out;
}

}  // namespace followbox
