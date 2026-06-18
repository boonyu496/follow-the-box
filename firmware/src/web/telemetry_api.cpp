#include "web/telemetry_api.h"

#include <cstdio>

#include "config/camera_config.h"

namespace followbox {

const char* modeToString(RunMode mode) {
  switch (mode) {
    case RunMode::BOOT_SELF_TEST:
      return "BOOT_SELF_TEST";
    case RunMode::SAFE_IDLE:
      return "SAFE_IDLE";
    case RunMode::MANUAL_RC:
      return "MANUAL_RC";
    case RunMode::MANUAL_H5_LOW_SPEED:
      return "MANUAL_H5_LOW_SPEED";
    case RunMode::MANUAL_CLOUD_LOW_SPEED:
      return "MANUAL_CLOUD_LOW_SPEED";
    case RunMode::AUTO_FOLLOW:
      return "AUTO_FOLLOW";
    case RunMode::FAULT_LOCKOUT:
      return "FAULT_LOCKOUT";
    case RunMode::ESTOP_ACTIVE:
      return "ESTOP_ACTIVE";
  }
  return "UNKNOWN";
}

const char* stopReasonToString(StopReason reason) {
  switch (reason) {
    case StopReason::NONE:
      return "NONE";
    case StopReason::ESTOP:
      return "ESTOP";
    case StopReason::RC_LOST:
      return "RC_LOST";
    case StopReason::H5_LOST:
      return "H5_LOST";
    case StopReason::CLOUD_LOST:
      return "CLOUD_LOST";
    case StopReason::UWB_LOST:
      return "UWB_LOST";
    case StopReason::OBSTACLE_STOP:
      return "OBSTACLE_STOP";
    case StopReason::LOW_BATTERY:
      return "LOW_BATTERY";
    case StopReason::SENSOR_TIMEOUT:
      return "SENSOR_TIMEOUT";
    case StopReason::MOTOR_FAULT:
      return "MOTOR_FAULT";
    case StopReason::INSTALL_WIZARD_NOT_DONE:
      return "INSTALL_WIZARD_NOT_DONE";
    case StopReason::WATCHDOG_TIMEOUT:
      return "WATCHDOG_TIMEOUT";
  }
  return "UNKNOWN";
}

size_t buildStateJson(const SystemState& state, char* out, size_t out_size) {
  if (out == nullptr || out_size == 0) {
    return 0;
  }

  const int written = std::snprintf(
      out, out_size,
      "{"
      "\"now_ms\":%u,"
      "\"mode\":\"%s\","
      "\"safety\":{\"motion_allowed\":%s,\"fault_latched\":%s,"
      "\"stop_reason\":\"%s\",\"max_speed_scale\":%.2f},"
      "\"rc\":{\"online\":%s,\"last_update_ms\":%u},"
      "\"cloud\":{\"connected\":%s,\"last_update_ms\":%u,\"last_seq\":%u},"
      "\"uwb\":{\"valid\":%s,\"distance_mm\":%d,\"bearing_deg\":%.1f,"
      "\"confidence\":%u,\"last_update_ms\":%u},"
      "\"obstacle\":{\"front_left_mm\":%d,\"front_center_mm\":%d,"
      "\"front_right_mm\":%d,\"side_left_mm\":%d,\"side_right_mm\":%d,"
      "\"valid\":%s,\"last_update_ms\":%u},"
      "\"lidar\":{\"valid\":%s,\"last_update_ms\":%u,"
      "\"front_left_mm\":%d,\"front_center_mm\":%d,"
      "\"front_right_mm\":%d,\"side_left_mm\":%d,\"side_right_mm\":%d,"
      "\"rx_bytes\":%u,\"packets\":%u,\"checksum_errors\":%u,"
      "\"framing_errors\":%u,\"scans\":%u},"
      "\"tof\":{\"valid\":%s,\"front_left_mm\":%d,\"front_center_mm\":%d,"
      "\"front_right_mm\":%d,\"front_left_valid\":%s,"
      "\"front_center_valid\":%s,\"front_right_valid\":%s,"
      "\"last_update_ms\":%u,\"init_ok_mask\":%u,\"init_attempt_count\":%u,"
      "\"init_failure_count\":%u,\"read_count\":%u,"
      "\"timeout_count\":%u,\"mux_nack_count\":%u,\"bus_clear_count\":%u,"
      "\"reinit_count\":%u,\"last_recovery_ms\":%u},"
      "\"ultrasonic\":{\"valid\":%s,\"left_mm\":%d,\"right_mm\":%d,"
      "\"left_valid\":%s,\"right_valid\":%s,\"last_update_ms\":%u},"
      "\"camera\":{\"online\":%s,\"stream_url\":\"%s\"},"
      "\"power\":{\"battery_voltage\":%.2f,\"low_battery\":%s},"
      "\"motor\":{\"enable\":%s,\"left_target\":%.2f,\"right_target\":%.2f,"
      "\"brake\":%s},"
      "\"install_wizard_complete\":%s,"
      "\"throttle_calibrated\":%s"
      "}",
      static_cast<unsigned>(state.now_ms), modeToString(state.mode),
      state.safety.motion_allowed ? "true" : "false",
      state.safety.fault_latched ? "true" : "false",
      stopReasonToString(state.safety.stop_reason),
      static_cast<double>(state.safety.max_speed_scale),
      state.rc.online ? "true" : "false",
      static_cast<unsigned>(state.rc.last_update_ms),
      state.cloud.connected ? "true" : "false",
      static_cast<unsigned>(state.cloud.last_update_ms),
      static_cast<unsigned>(state.cloud.last_seq),
      state.uwb.valid ? "true" : "false", state.uwb.distance_mm,
      static_cast<double>(state.uwb.bearing_deg),
      static_cast<unsigned>(state.uwb.confidence),
      static_cast<unsigned>(state.uwb.last_update_ms),
      state.obstacle.front_left_mm,
      state.obstacle.front_center_mm, state.obstacle.front_right_mm,
      state.obstacle.side_left_mm, state.obstacle.side_right_mm,
      state.obstacle.valid ? "true" : "false",
      static_cast<unsigned>(state.obstacle.last_update_ms),
      state.sensor_diagnostics.lidar_valid ? "true" : "false",
      static_cast<unsigned>(state.sensor_diagnostics.lidar_last_update_ms),
      state.sensor_diagnostics.lidar_front_left_mm,
      state.sensor_diagnostics.lidar_front_center_mm,
      state.sensor_diagnostics.lidar_front_right_mm,
      state.sensor_diagnostics.lidar_side_left_mm,
      state.sensor_diagnostics.lidar_side_right_mm,
      static_cast<unsigned>(state.sensor_diagnostics.lidar_rx_bytes),
      static_cast<unsigned>(state.sensor_diagnostics.lidar_packets),
      static_cast<unsigned>(state.sensor_diagnostics.lidar_checksum_errors),
      static_cast<unsigned>(state.sensor_diagnostics.lidar_framing_errors),
      static_cast<unsigned>(state.sensor_diagnostics.lidar_scans),
      state.tof.valid ? "true" : "false", state.tof.front_left_mm,
      state.tof.front_center_mm, state.tof.front_right_mm,
      state.tof.front_left_valid ? "true" : "false",
      state.tof.front_center_valid ? "true" : "false",
      state.tof.front_right_valid ? "true" : "false",
      static_cast<unsigned>(state.tof.last_update_ms),
      static_cast<unsigned>(state.sensor_diagnostics.tof_init_ok_mask),
      static_cast<unsigned>(state.sensor_diagnostics.tof_init_attempt_count),
      static_cast<unsigned>(state.sensor_diagnostics.tof_init_failure_count),
      static_cast<unsigned>(state.sensor_diagnostics.tof_read_count),
      static_cast<unsigned>(state.sensor_diagnostics.tof_timeout_count),
      static_cast<unsigned>(state.sensor_diagnostics.tof_mux_nack_count),
      static_cast<unsigned>(state.sensor_diagnostics.tof_bus_clear_count),
      static_cast<unsigned>(state.sensor_diagnostics.tof_reinit_count),
      static_cast<unsigned>(state.sensor_diagnostics.tof_last_recovery_ms),
      state.ultrasonic.valid ? "true" : "false", state.ultrasonic.left_mm,
      state.ultrasonic.right_mm,
      state.ultrasonic.left_valid ? "true" : "false",
      state.ultrasonic.right_valid ? "true" : "false",
      static_cast<unsigned>(state.ultrasonic.last_update_ms),
      state.camera.online ? "true" : "false",
      camera_config::STREAM_URL,
      static_cast<double>(state.power.battery_voltage),
      state.power.low_battery ? "true" : "false",
      state.motor_command.enable ? "true" : "false",
      static_cast<double>(state.motor_command.left_target),
      static_cast<double>(state.motor_command.right_target),
      state.motor_command.brake ? "true" : "false",
      state.install_wizard_complete ? "true" : "false",
      state.throttle_calibrated ? "true" : "false");

  if (written < 0 || static_cast<size_t>(written) >= out_size) {
    // Truncated: do not hand back a malformed fragment.
    out[0] = '\0';
    return 0;
  }
  return static_cast<size_t>(written);
}

}  // namespace followbox
