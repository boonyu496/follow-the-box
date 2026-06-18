#include <Arduino.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>

#include "app/app.h"
#include "app/shared_state.h"
#include "config/network_config.h"
#include "cloud/cloud_client.h"
#include "control/rc_input_ds600.h"
#include "drive/drive_adapter_analog_bldc.h"
#include "ota/cloud_ota_manager.h"
#include "sensors/sensor_task.h"
#include "storage/calibration_store.h"
#include "storage/profile_store.h"
#include "storage/wifi_store.h"
#include "telemetry/debug_console.h"
#include "telemetry/telemetry_logger.h"
#include "web/h5_web_server.h"

namespace {

using namespace followbox;

App app;
SensorTask sensors;
RcInputDs600 rc_input;
DriveAdapterAnalogBldc drive;
H5WebServer h5_web;
CloudClient cloud_client;
CloudOtaManager cloud_ota;
SharedState shared;
ProfileStore profile_store;
CalibrationStore calibration_store;
WifiStore wifi_store;
TelemetryLogger telemetry_logger;

// Fixed task periods.
constexpr TickType_t kControlPeriod = pdMS_TO_TICKS(20);  // 50 Hz
constexpr TickType_t kSensorPeriod = pdMS_TO_TICKS(20);   // 50 Hz
constexpr TickType_t kCommPeriod = pdMS_TO_TICKS(20);     // 50 Hz; OTA needs frequent polling.

volatile bool g_ota_in_progress = false;

void setOtaSafetyActive(bool active) {
  g_ota_in_progress = active;
}

void beginOtaService() {
  if (!net::OTA_ENABLED) {
    return;
  }

  ArduinoOTA.setHostname(net::OTA_HOSTNAME);
  ArduinoOTA.setPort(net::OTA_PORT);
  ArduinoOTA.setPassword(net::OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    g_ota_in_progress = true;
    if (ArduinoOTA.getCommand() == U_SPIFFS) {
      // Filesystem update: unmount so the writer never races a mounted FS.
      LittleFS.end();
    }
    Serial.println("followbox ota: start, motor output forced safe");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("followbox ota: end");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    g_ota_in_progress = true;
    Serial.printf("followbox ota: error %u, staying safe\r\n",
                  static_cast<unsigned>(error));
  });
  ArduinoOTA.begin();
}

// --- Core 0: sensor + RC ingestion -----------------------------------------
// Drains the UWB/lidar/IMU UARTs and the battery ADC and reads the
// interrupt-captured RC channels, then publishes one consistent SensorBundle.
// Never touches the motor path. Blocking-prone I/O lives here, off the control
// core, so the control loop reads only committed snapshots (FIRMWARE-SPEC 7.1).
void sensorTaskEntry(void*) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    sensors.update(now);
    rc_input.update(now);

    SensorBundle bundle;
    bundle.uwb = sensors.uwbTarget();
    bundle.obstacle = sensors.obstacle();
    bundle.tof = sensors.tof();
    bundle.sensor_diagnostics = sensors.diagnostics();
    bundle.ultrasonic = sensors.ultrasonic();
    bundle.camera = sensors.camera();
    bundle.power = sensors.power();
    bundle.imu = sensors.imu();
    bundle.rc = rc_input.getSnapshot();
    bundle.estop_active = sensors.estopActive();
    bundle.sensor_heartbeat_ms = sensors.sensorHeartbeatMs();
    bundle.uwb_heartbeat_ms = sensors.uwbHeartbeatMs();
    shared.publishSensors(bundle);

    vTaskDelayUntil(&last, kSensorPeriod);
  }
}

// --- Core 0: telemetry push -------------------------------------------------
// Reads the committed SystemState copy and pushes /ws/state to connected
// panels. Low priority; can never block or starve the control loop. The H5 POST
// callbacks run separately in the AsyncTCP task (also Core 0).
void commTaskEntry(void*) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    ArduinoOTA.handle();
    const SystemState state = shared.latestState();
    h5_web.pushState(state, now);
    cloud_client.update(state, now);
    cloud_ota.update(now);
    // Off the control core: rate-limited + transition-triggered state log.
    telemetry_logger.update(state, now);
    vTaskDelayUntil(&last, kCommPeriod);
  }
}

// --- Core 1: real-time control + safety + motor output ----------------------
// Sole owner of the live SystemState and the ONLY place motor output is written
// (FIRMWARE-SPEC 7). Reads committed sensor/H5 snapshots (no blocking I/O on
// this core), runs the safety/mode/pipeline/mixer chain, drives the PWM, then
// publishes a SystemState copy for the comm task.
void controlTaskEntry(void*) {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    if (g_ota_in_progress) {
      drive.stopNow();
      shared.publishState(app.state());
      vTaskDelayUntil(&last, kControlPeriod);
      continue;
    }
    const SensorBundle bundle = shared.latestSensors();
    app.ingestSensorInputs(bundle.uwb, bundle.obstacle, bundle.power, bundle.imu,
                           bundle.tof, bundle.sensor_diagnostics,
                           bundle.ultrasonic, bundle.camera,
                           bundle.estop_active,
                           bundle.sensor_heartbeat_ms, bundle.uwb_heartbeat_ms);
    app.ingestRcInput(bundle.rc);
    // H5 input copied under the web server's own lock; staleness applied there.
    app.ingestH5Input(h5_web.pollInput(now, app, drive));
    app.ingestCloudInput(cloud_client.pollInput(now));

    // Deferred calibrate/wizard from H5 — executed in control-loop context
    // (CODE-REVIEW-H5-2026-06-15 P0-1). Transport layer only sets flags;
    // drive/app calls happen here, after safety evaluation.
    {
      const auto& h5 = app.state().h5;
      if (h5.pending_calibrate) {
        ThrottleCalibration cal;
        cal.deadband_mv = h5.cal_deadband_mv;
        cal.min_active_mv = h5.cal_min_active_mv;
        cal.max_mv = h5.cal_max_mv;
        cal.module_full_scale_mv = h5.cal_module_full_scale_mv;
        cal.rise_mv_per_s = h5.cal_rise_mv_per_s;
        cal.fall_mv_per_s = h5.cal_fall_mv_per_s;
        drive.setCalibration(cal);
        app.setThrottleCalibrated(true);
      }
      if (h5.pending_wizard_complete) {
        app.setInstallWizardComplete(true);
      }
    }

    app.tick(now);
    // motor_command is already safety-gated (applyFinalGate); drive_adapter is
    // the only PWM exit and re-checks enable/brake itself.
    drive.writeCommand(app.state().motor_command, now);
    shared.publishState(app.state());

    vTaskDelayUntil(&last, kControlPeriod);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  DebugConsole::begin(LogLevel::kInfo);
  telemetry_logger.begin();
  app.begin();

  // Persistence: load before the control task starts. Both stores fall back to
  // safe defaults if NVS is blank/corrupt (wizard=false locks out AUTO_FOLLOW;
  // calibration is clamped to the 0-5V module window).
  profile_store.begin();
  const RuntimeProfile prof = profile_store.load();
  app.setInstallWizardComplete(prof.install_wizard_complete);

  calibration_store.begin();
  drive.setCalibration(calibration_store.load());
  app.setThrottleCalibrated(calibration_store.isCalibrated());

  sensors.begin();
  rc_input.begin();
  // begin() leaves the drive safe: enable off, brake engaged, throttle 0.
  drive.begin();
  // H5 transport: WiFi + async HTTP/WS (AsyncTCP task on Core 0). Never sets
  // PWM, clears the e-stop, or bypasses the wizard (gated downstream).
  wifi_store.begin();
  h5_web.begin(&profile_store, &calibration_store, &wifi_store);
  cloud_client.begin();
  cloud_ota.begin(setOtaSafetyActive);
  beginOtaService();

  // Core 1 = real-time control/safety (highest priority). Core 0 = blocking
  // I/O and comms, so neither can stall the 50 Hz motor loop.
  xTaskCreatePinnedToCore(controlTaskEntry, "control", 4096, nullptr,
                          configMAX_PRIORITIES - 1, nullptr, 1);
  xTaskCreatePinnedToCore(sensorTaskEntry, "sensor", 4096, nullptr, 3, nullptr,
                          0);
  // 12 KB: the comm task hosts HTTPClient (cloud upload/poll) whose connect/
  // TLS paths need far more than the old 4 KB; telemetry buffers themselves
  // are static in CloudClient, this headroom is for the network stack calls.
  xTaskCreatePinnedToCore(commTaskEntry, "comm", 12288, nullptr, 1, nullptr, 0);
}

void loop() {
  // All work runs in the pinned tasks; keep the Arduino loopTask idle.
  vTaskDelay(pdMS_TO_TICKS(1000));
}
