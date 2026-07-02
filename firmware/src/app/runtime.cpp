#include "app/runtime.h"

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>
#include <esp_system.h>

#include <atomic>

#include "config/network_config.h"
#include "config/ota_config.h"
#include "telemetry/debug_console.h"
#include "web/wifi_ap_supervisor.h"

namespace followbox {

namespace {

constexpr TickType_t kControlPeriod = pdMS_TO_TICKS(20);  // 50 Hz
constexpr TickType_t kSensorPeriod = pdMS_TO_TICKS(20);   // 50 Hz
constexpr TickType_t kCommPeriod = pdMS_TO_TICKS(20);     // 50 Hz; OTA polling.

std::atomic<bool> g_ota_in_progress{false};

// Reset counter kept in RTC RAM so it survives WDT/PANIC/BROWNOUT resets (only
// a full power cycle / brownout that drops the RTC domain clears it). If a
// phone joining the hotspot silently reboots the box, this climbs -> proof of
// a reboot rather than the phone simply leaving a healthy AP.
RTC_DATA_ATTR uint32_t g_rtc_boot_count = 0;

void setOtaSafetyActive(bool active) {
  g_ota_in_progress.store(active);
}

// Human-readable label for the ESP-IDF reset reason. Logged once at boot so a
// crash that only shows up in the field (e.g. a phone joining the hotspot then
// the AP "disappearing") leaves a breadcrumb in the /api/logs ring: BROWNOUT
// points at a marginal power/USB supply, TASK_WDT/INT_WDT/PANIC at a firmware
// fault, and POWERON/SW at a normal (re)boot.
const char* resetReasonLabel(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

}  // namespace

void Runtime::begin() {
  Serial.begin(115200);
  DebugConsole::begin(LogLevel::kInfo);
  // Boot breadcrumb: firmware version + why the chip last reset. Lands in the
  // DebugConsole ring so it is visible over serial AND via GET /api/logs, which
  // is how a field hotspot-drop repro can be classified (BROWNOUT vs WDT/PANIC
  // vs a clean reboot) without the native-USB console attached.
  const esp_reset_reason_t reset_reason = esp_reset_reason();
  const char* reset_label = resetReasonLabel(reset_reason);
  ++g_rtc_boot_count;
  FB_LOGI("boot: fw=%s reset_reason=%s(%d) boot_count=%lu",
          ota_config::CURRENT_VERSION, reset_label,
          static_cast<int>(reset_reason),
          static_cast<unsigned long>(g_rtc_boot_count));
  // Persist for /api/wifi/status so the reset reason survives the log ring
  // scrolling out under high-frequency telemetry logging.
  setBootDiag(reset_label, g_rtc_boot_count);
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
