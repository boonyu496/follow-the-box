#include "app/runtime.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>

#include <atomic>

#include "config/network_config.h"
#include "telemetry/debug_console.h"

namespace followbox {

namespace {

constexpr TickType_t kControlPeriod = pdMS_TO_TICKS(20);  // 50 Hz
constexpr TickType_t kSensorPeriod = pdMS_TO_TICKS(20);   // 50 Hz
constexpr TickType_t kCommPeriod = pdMS_TO_TICKS(20);     // 50 Hz; OTA polling.

std::atomic<bool> g_ota_in_progress{false};

void setOtaSafetyActive(bool active) {
  g_ota_in_progress.store(active);
}

}  // namespace

void Runtime::begin() {
  Serial.begin(115200);
  DebugConsole::begin(LogLevel::kInfo);
  telemetry_logger_.begin();
  app_.begin();

  // Load safe persisted defaults before the control task starts.
  profile_store_.begin();
  const RuntimeProfile prof = profile_store_.load();
  app_.setInstallWizardComplete(prof.install_wizard_complete);

  calibration_store_.begin();
  drive_.setCalibration(calibration_store_.load());
  app_.setThrottleCalibrated(calibration_store_.isCalibrated());

  drive_.begin();
  wifi_store_.begin();
  cloud_client_.begin();
  cloud_ota_.begin(setOtaSafetyActive);
  h5_web_.begin(&profile_store_, &calibration_store_, &wifi_store_,
                &cloud_ota_);
  beginOtaService();
  sensors_.begin();
  rc_input_.begin();

  xTaskCreatePinnedToCore(controlTaskEntry, "control", 4096, this,
                          configMAX_PRIORITIES - 1, nullptr, 1);
  xTaskCreatePinnedToCore(sensorTaskEntry, "sensor", 4096, this, 3, nullptr,
                          0);
  // 24 KB: AsyncWebServer pushes plus HTTPClient cloud/OTA calls.
  xTaskCreatePinnedToCore(commTaskEntry, "comm", 24576, this, 1, nullptr, 0);
}

void Runtime::loopIdle() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}

void Runtime::beginOtaService() {
  if (!net::OTA_ENABLED) {
    return;
  }

  ArduinoOTA.setHostname(net::OTA_HOSTNAME);
  ArduinoOTA.setPort(net::OTA_PORT);
  ArduinoOTA.setPassword(net::OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    g_ota_in_progress.store(true);
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
    g_ota_in_progress.store(true);
    Serial.printf("followbox ota: error %u, staying safe\r\n",
                  static_cast<unsigned>(error));
  });
  ArduinoOTA.begin();
}

void Runtime::sensorTaskEntry(void* context) {
  static_cast<Runtime*>(context)->sensorTaskLoop();
}

void Runtime::commTaskEntry(void* context) {
  static_cast<Runtime*>(context)->commTaskLoop();
}

void Runtime::controlTaskEntry(void* context) {
  static_cast<Runtime*>(context)->controlTaskLoop();
}

void Runtime::sensorTaskLoop() {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    sensors_.update(now);
    const uint32_t rc_now = millis();
    rc_input_.update(rc_now);

    SensorBundle bundle;
    bundle.uwb = sensors_.uwbTarget();
    bundle.obstacle = sensors_.obstacle();
    bundle.tof = sensors_.tof();
    bundle.sensor_diagnostics = sensors_.diagnostics();
    bundle.ultrasonic = sensors_.ultrasonic();
    bundle.camera = sensors_.camera();
    bundle.power = sensors_.power();
    bundle.imu = sensors_.imu();
    bundle.rc = rc_input_.getSnapshot();
    bundle.estop_active = sensors_.estopActive();
    bundle.sensor_heartbeat_ms = sensors_.sensorHeartbeatMs();
    bundle.uwb_heartbeat_ms = sensors_.uwbHeartbeatMs();
    shared_.publishSensors(bundle);

    vTaskDelayUntil(&last, kSensorPeriod);
  }
}

void Runtime::commTaskLoop() {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    ArduinoOTA.handle();
    const SystemState state = shared_.latestState();
    h5_web_.pushState(state, now);
    cloud_client_.update(state, now);
    cloud_ota_.update(now);
    telemetry_logger_.update(state, now);
    vTaskDelayUntil(&last, kCommPeriod);
  }
}

void Runtime::controlTaskLoop() {
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    const uint32_t now = millis();
    if (g_ota_in_progress.load()) {
      drive_.stopNow();
      shared_.publishState(app_.state());
      vTaskDelayUntil(&last, kControlPeriod);
      continue;
    }

    const SensorBundle bundle = shared_.latestSensors();
    app_.ingestSensorInputs(bundle.uwb, bundle.obstacle, bundle.power,
                            bundle.imu, bundle.tof,
                            bundle.sensor_diagnostics, bundle.ultrasonic,
                            bundle.camera, bundle.estop_active,
                            bundle.sensor_heartbeat_ms,
                            bundle.uwb_heartbeat_ms);
    app_.ingestRcInput(bundle.rc);
    app_.ingestH5Input(h5_web_.pollInput(now, app_, drive_));
    app_.ingestCloudInput(cloud_client_.pollInput(now));

    const auto& h5 = app_.state().h5;
    if (h5.pending_calibrate) {
      ThrottleCalibration cal;
      cal.deadband_mv = h5.cal_deadband_mv;
      cal.min_active_mv = h5.cal_min_active_mv;
      cal.max_mv = h5.cal_max_mv;
      cal.module_full_scale_mv = h5.cal_module_full_scale_mv;
      cal.rise_mv_per_s = h5.cal_rise_mv_per_s;
      cal.fall_mv_per_s = h5.cal_fall_mv_per_s;
      drive_.setCalibration(cal);
      app_.setThrottleCalibrated(true);
    }
    if (h5.pending_wizard_complete) {
      app_.setInstallWizardComplete(true);
    }

    app_.tick(now);
    drive_.writeCommand(app_.state().motor_command, now);
    shared_.publishState(app_.state());

    vTaskDelayUntil(&last, kControlPeriod);
  }
}

}  // namespace followbox
