#pragma once

#include "app/app.h"
#include "app/shared_state.h"
#include "cloud/cloud_client.h"
#include "control/rc_input_ds600.h"
#include "drive/drive_adapter_analog_bldc.h"
#include "ota/cloud_ota_manager.h"
#include "sensors/sensor_task.h"
#include "storage/calibration_store.h"
#include "storage/profile_store.h"
#include "storage/wifi_store.h"
#include "telemetry/telemetry_logger.h"
#include "web/h5_web_server.h"

namespace followbox {

// Runtime owns the Arduino/FreeRTOS wiring around the pure app pipeline.
// main.cpp remains the Arduino entry only; controlTaskLoop remains the sole
// place that writes the final, safety-gated MotorCommand to the drive adapter.
class Runtime {
 public:
  void begin();
  void loopIdle();

 private:
  static void controlTaskEntry(void* context);
  static void sensorTaskEntry(void* context);
  static void commTaskEntry(void* context);

  void beginOtaService();
  void controlTaskLoop();
  void sensorTaskLoop();
  void commTaskLoop();

  App app_;
  SensorTask sensors_;
  RcInputDs600 rc_input_;
  DriveAdapterAnalogBldc drive_;
  H5WebServer h5_web_;
  CloudClient cloud_client_;
  CloudOtaManager cloud_ota_;
  SharedState shared_;
  ProfileStore profile_store_;
  CalibrationStore calibration_store_;
  WifiStore wifi_store_;
  TelemetryLogger telemetry_logger_;
};

}  // namespace followbox
