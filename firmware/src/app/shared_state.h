#pragma once

#include "core/system_state.h"
#include "core/types.h"

namespace followbox {

// Complete sensor snapshot handed from the Core 0 sensor task to the Core 1
// control task. One struct = one atomic publish, so the control loop only ever
// sees a fully consistent set of inputs (FIRMWARE-SPEC 7.1 double-buffer rule).
struct SensorBundle {
  UwbTarget uwb;
  ObstacleSnapshot obstacle;
  TofSnapshot tof;
  SensorDiagnostics sensor_diagnostics;
  UltrasonicSnapshot ultrasonic;
  CameraStatus camera;
  PowerStatus power;
  ImuSnapshot imu;
  RcInput rc;
  bool estop_active = true;
  uint32_t sensor_heartbeat_ms = 0;
  uint32_t uwb_heartbeat_ms = 0;
};

// Cross-core mailboxes between the pinned FreeRTOS tasks. Each publish/read is a
// single struct copy under a portMUX spinlock - the critical section never does
// I/O, parsing, WebSocket or logging (FIRMWARE-SPEC 7.1). Two independent slots:
//   - sensors: written by sensor_task (Core 0), read by control_task (Core 1)
//   - state:   written by control_task (Core 1), read by comm_task (Core 0)
// control_task remains the sole owner/writer of the live SystemState; comm only
// ever sees a committed copy, so it never races the motion path.
class SharedState {
 public:
  void publishSensors(const SensorBundle& bundle);
  SensorBundle latestSensors() const;

  void publishState(const SystemState& state);
  SystemState latestState() const;
};

}  // namespace followbox
