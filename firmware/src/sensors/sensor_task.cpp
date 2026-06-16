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

}  // namespace followbox
