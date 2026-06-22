#include "sensors/sensor_task.h"

#include <cstddef>

#include "config/board_pins.h"
#include "config/profile_defaults.h"
#include "core/time_utils.h"
#include "telemetry/debug_console.h"

namespace followbox {

namespace {
constexpr uint8_t kLidarStartCommand[] = {0xA5, 0x60};
constexpr uint32_t kLidarStartupGraceMs = 3000;
constexpr uint32_t kLidarDiagPeriodMs = 5000;

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
}  // namespace

SensorTask::SensorTask()
    : uwb_uart_(pins::UART_NUM_UWB, pins::PIN_UWB_RX, pins::PIN_UWB_TX,
                profile::UWB_UART_BAUD),
      lidar_uart_(pins::UART_NUM_LIDAR, pins::PIN_LIDAR_RX, pins::PIN_LIDAR_TX,
                  profile::LIDAR_UART_BAUD),
      imu_uart_(pins::UART_NUM_IMU, pins::PIN_IMU_RX, pins::PIN_IMU_TX,
                profile::IMU_UART_BAUD),
      estop_status_(pins::PIN_ESTOP_STATUS, ActiveLevel::ACTIVE_HIGH,
                    InputPull::PULL_UP) {}

void SensorTask::begin() {
  uwb_parser_.reset();
  lidar_.reset();
  imu_.reset();
  camera_.reset();
  estop_status_.begin();
  estop_active_ = true;
  uwb_uart_.begin();
  if (lidar_uart_.begin()) {
    const size_t written = sendLidarStartCommand();
    FB_LOGI(
        "LIDAR begin: uart=%d rx=GPIO%d tx=GPIO%d baud=%lu start=A560 "
        "written=%u",
        pins::UART_NUM_LIDAR, pins::PIN_LIDAR_RX, pins::PIN_LIDAR_TX,
        static_cast<unsigned long>(profile::LIDAR_UART_BAUD),
        static_cast<unsigned>(written));
    if (written != sizeof(kLidarStartCommand)) {
      FB_LOGW("LIDAR begin start_write_short expected=%u written=%u",
              static_cast<unsigned>(sizeof(kLidarStartCommand)),
              static_cast<unsigned>(written));
    }
  } else if (lidar_uart_.isEnabled()) {
    FB_LOGW("LIDAR begin failed: uart=%d rx=GPIO%d tx=GPIO%d baud=%lu",
            pins::UART_NUM_LIDAR, pins::PIN_LIDAR_RX, pins::PIN_LIDAR_TX,
            static_cast<unsigned long>(profile::LIDAR_UART_BAUD));
  }
  imu_uart_.begin();
  power_monitor_.begin();
  tof_.begin();
  ultrasonic_.begin();
}

void SensorTask::update(uint32_t now_ms) {
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

size_t SensorTask::sendLidarStartCommand() {
  // EaiLidarTest V1.12.3 sends A5 60 immediately before S2 data appears.
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

  if (now_ms >= kLidarStartupGraceMs) {
    if (stats.rx_byte_count == 0) {
      FB_LOGW(
          "LIDAR diag no_rx rx=0 packets=0 scans=0 baud=%lu "
          "check DATA/TX->GPIO%d, CTL/RX->GPIO%d, 5V/GND, common ground, "
          "and lidar motor/start",
          static_cast<unsigned long>(profile::LIDAR_UART_BAUD),
          pins::PIN_LIDAR_RX, pins::PIN_LIDAR_TX);
    } else if (stats.packet_count == 0) {
      FB_LOGW(
          "LIDAR diag rx_no_packets rx=%lu(+%lu) aa55=%lu ld54=%lu "
          "framing=%lu(+%lu) checksum=%lu(+%lu) baud=%lu",
          static_cast<unsigned long>(stats.rx_byte_count),
          static_cast<unsigned long>(delta_rx),
          static_cast<unsigned long>(stats.aa55_header_count),
          static_cast<unsigned long>(stats.ld_header_count),
          static_cast<unsigned long>(stats.framing_error_count),
          static_cast<unsigned long>(delta_framing_errors),
          static_cast<unsigned long>(stats.checksum_error_count),
          static_cast<unsigned long>(delta_checksum_errors),
          static_cast<unsigned long>(profile::LIDAR_UART_BAUD));
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
    } else if (stats.scan_count == 0) {
      FB_LOGW(
          "LIDAR diag packets_no_scan rx=%lu packets=%lu(+%lu) scans=0 "
          "last_packet=%lu check AA55 intensity8 10+LSN*3 frames and start command A560",
          static_cast<unsigned long>(stats.rx_byte_count),
          static_cast<unsigned long>(stats.packet_count),
          static_cast<unsigned long>(delta_packets),
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
          "LIDAR diag ok rx=%lu packets=%lu scans=%lu sectors=%d/%d/%d/%d/%d",
          static_cast<unsigned long>(stats.rx_byte_count),
          static_cast<unsigned long>(stats.packet_count),
          static_cast<unsigned long>(stats.scan_count), lidar.front_left_mm,
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
