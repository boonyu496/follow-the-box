#pragma once

#include <cstddef>
#include <cstdint>

#include "core/types.h"
#include "hal/gpio_in.h"
#include "hal/uart_bus.h"
#include "sensors/camera_link.h"
#include "sensors/lidar_bringup_probe.h"
#include "sensors/jy61p_imu.h"
#include "sensors/lidar_eai_s2.h"
#include "sensors/obstacle_fusion.h"
#include "sensors/power_monitor.h"
#include "sensors/tof_vl53l1x_array.h"
#include "sensors/ultrasonic_array.h"
#include "sensors/uwb_gc_p2304.h"

namespace followbox {

// Sensor ingestion task: UART bytes -> pure-logic parsers -> SystemState inputs.
//
// Owns the read-only UWB and (optional) lidar UART streams and the matching
// parsers. update() drains buffered bytes, runs the staleness timeouts, and
// refreshes the task heartbeats consumed by the safety watchdog. It produces
// snapshots only; the control loop (App) ingests them and remains the sole
// owner of SystemState. No GPIO/motion access lives here.
class SensorTask {
 public:
  SensorTask();

  // Open the UART streams. The optional lidar bus self-skips when unassigned.
  void begin();

  // Drain pending bytes (bounded), apply timeouts, refresh heartbeats.
  void update(uint32_t now_ms);

  // Latest parsed snapshots for the control loop to ingest.
  const UwbTarget& uwbTarget() const { return uwb_parser_.target(); }
  // Fused obstacle picture (lidar + forward TOF + side ultrasonic). The motion
  // path (SafetyManager / ObstacleManager) consumes this single snapshot.
  const ObstacleSnapshot& obstacle() const { return fused_obstacle_; }
  PowerStatus power() const { return power_monitor_.getSnapshot(); }
  const ImuSnapshot& imu() const { return imu_.snapshot(); }
  const TofSnapshot& tof() const { return tof_.snapshot(); }
  SensorDiagnostics diagnostics() const;
  const UltrasonicSnapshot& ultrasonic() const { return ultrasonic_.snapshot(); }
  const CameraStatus& camera() const { return camera_.status(); }
  bool estopActive() const { return estop_active_; }

  // Forward ESP32-S3-CAM link evidence (video only; never a safety input).
  void noteCameraHeartbeat(uint32_t now_ms) { camera_.noteHeartbeat(now_ms); }

  // Heartbeats (ms of last completed loop) for the safety watchdog.
  uint32_t sensorHeartbeatMs() const { return sensor_heartbeat_ms_; }
  uint32_t uwbHeartbeatMs() const { return uwb_heartbeat_ms_; }

  // Bring-up diagnostics for telemetry/H5.
  const UwbParserStats& uwbStats() const { return uwb_parser_.stats(); }
  const LidarS2Stats& lidarStats() const { return lidar_.stats(); }
  const ImuStats& imuStats() const { return imu_.stats(); }
  const TofStats& tofStats() const { return tof_.stats(); }
  const UltrasonicStats& ultrasonicStats() const { return ultrasonic_.stats(); }
  const CameraLinkStats& cameraStats() const { return camera_.stats(); }

 private:
  void drainUwb(uint32_t now_ms);
  void drainLidar(uint32_t now_ms);
  void drainImu(uint32_t now_ms);

  UartBus uwb_uart_;
  UartBus lidar_uart_;
  UartBus imu_uart_;
  GpioIn estop_status_;
  UwbGcP2304Parser uwb_parser_;
  LidarEaiS2 lidar_;
  LidarBringupProbe lidar_probe_;
  Jy61pImu imu_;
  PowerMonitor power_monitor_;
  TofVl53l1xArray tof_;
  UltrasonicArray ultrasonic_;
  CameraLink camera_;
  ObstacleSnapshot fused_obstacle_;
  bool estop_active_ = true;
  uint32_t sensor_heartbeat_ms_ = 0;
  uint32_t uwb_heartbeat_ms_ = 0;
  uint32_t last_slow_update_log_ms_ = 0;
};

}  // namespace followbox
