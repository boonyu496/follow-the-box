#include "sensors/sensor_task.h"

#include "config/board_pins.h"
#include "config/profile_defaults.h"

namespace followbox {

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
  lidar_uart_.begin();
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
