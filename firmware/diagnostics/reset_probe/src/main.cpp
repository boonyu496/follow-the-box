#include <Arduino.h>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_sleep.h"
#include "esp_system.h"

namespace {

constexpr uint32_t kRtcMagic = 0xF0110B0AUL;
constexpr uint32_t kHeartbeatPeriodMs = 1000;

RTC_DATA_ATTR uint32_t rtc_magic = 0;
RTC_DATA_ATTR uint32_t boot_count = 0;
RTC_DATA_ATTR uint32_t previous_uptime_ms = 0;
RTC_DATA_ATTR uint32_t previous_alive_seq = 0;

uint32_t alive_seq = 0;
uint32_t last_print_ms = 0;

const char* resetReasonText(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "POWERON: power-on or power interruption";
    case ESP_RST_EXT:
      return "EXT: external reset / EN pin";
    case ESP_RST_SW:
      return "SW: software restart";
    case ESP_RST_PANIC:
      return "PANIC: program crash";
    case ESP_RST_INT_WDT:
      return "INT_WDT: interrupt watchdog";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT: task watchdog";
    case ESP_RST_WDT:
      return "WDT: other watchdog";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP: deep-sleep wake";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT: voltage drop";
    case ESP_RST_SDIO:
      return "SDIO: SDIO reset";
    default:
      return "UNKNOWN: unknown reset reason";
  }
}

const char* resetReasonHint(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "If this repeats unexpectedly, inspect USB/5V power and board power path.";
    case ESP_RST_EXT:
      return "Inspect EN/reset button, reset wiring, auto-program circuit, and loose jumpers.";
    case ESP_RST_SW:
      return "A firmware path called ESP.restart(), esp_restart(), OTA reboot, or similar.";
    case ESP_RST_PANIC:
      return "Firmware crashed. Capture the backtrace printed immediately before reboot.";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return "A task/interrupt likely blocked too long. Check I/O waits and tight loops.";
    case ESP_RST_DEEPSLEEP:
      return "Expected only after the 'd' serial command or intentional deep sleep.";
    case ESP_RST_BROWNOUT:
      return "Power dipped. Test with motors/controllers disconnected and a short known-good cable.";
    default:
      return "No specific hint. Compare probe stability against the main firmware.";
  }
}

void printBytes(const char* label, uint32_t bytes) {
  Serial.printf("%-22s %10lu bytes (%lu KB)\r\n", label,
                static_cast<unsigned long>(bytes),
                static_cast<unsigned long>(bytes / 1024));
}

void printChipInfo() {
  esp_chip_info_t chip;
  esp_chip_info(&chip);

  uint32_t flash_size = 0;
  const esp_err_t flash_status = esp_flash_get_size(nullptr, &flash_size);

  Serial.println("Chip:");
  Serial.printf("  Model/revision:       ESP32-S3 rev %d\r\n", chip.revision);
  Serial.printf("  Cores:                %d\r\n", chip.cores);
  Serial.printf("  CPU frequency:        %lu MHz\r\n",
                static_cast<unsigned long>(ESP.getCpuFreqMHz()));
  Serial.printf("  SDK version:          %s\r\n", ESP.getSdkVersion());
  if (flash_status == ESP_OK) {
    Serial.printf("  Flash size detected:  %lu bytes (%lu MB)\r\n",
                  static_cast<unsigned long>(flash_size),
                  static_cast<unsigned long>(flash_size / 1024 / 1024));
  } else {
    Serial.printf("  Flash size detected:  ERROR %d\r\n",
                  static_cast<int>(flash_status));
  }
  Serial.printf("  PSRAM found:          %s\r\n", psramFound() ? "yes" : "no");
}

void printMemoryInfo() {
  Serial.println("Memory:");
  printBytes("  Heap total:", ESP.getHeapSize());
  printBytes("  Heap free:", ESP.getFreeHeap());
  printBytes("  Heap min free:", ESP.getMinFreeHeap());
  printBytes("  Heap max alloc:", ESP.getMaxAllocHeap());
  printBytes("  Internal free:",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  printBytes("  Internal min free:",
             heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
  printBytes("  PSRAM total:",
             heap_caps_get_total_size(MALLOC_CAP_SPIRAM));
  printBytes("  PSRAM free:",
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  Serial.printf("  Loop stack high-water: %lu bytes\r\n",
                static_cast<unsigned long>(
                    uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
}

void printHelp() {
  Serial.println("Serial commands:");
  Serial.println("  s = print snapshot");
  Serial.println("  r = software restart, expect RESET REASON SW");
  Serial.println("  p = abort/panic, expect RESET REASON PANIC");
  Serial.println("  d = deep sleep 3s, expect RESET REASON DEEPSLEEP");
  Serial.println("  h = help");
}

void printSnapshot(const char* title) {
  const esp_reset_reason_t reason = esp_reset_reason();
  const esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();

  Serial.println();
  Serial.println("==================================================");
  Serial.println(title);
  Serial.printf("Boot count:           %lu\r\n",
                static_cast<unsigned long>(boot_count));
  Serial.printf("Previous uptime:      %lu ms\r\n",
                static_cast<unsigned long>(previous_uptime_ms));
  Serial.printf("Previous alive seq:   %lu\r\n",
                static_cast<unsigned long>(previous_alive_seq));
  Serial.printf("Reset reason:         %s (%d)\r\n",
                resetReasonText(reason), static_cast<int>(reason));
  Serial.printf("Reset hint:           %s\r\n", resetReasonHint(reason));
  Serial.printf("Wakeup cause:         %d\r\n", static_cast<int>(wakeup));
  Serial.printf("Current uptime:       %lu ms\r\n",
                static_cast<unsigned long>(millis()));
  Serial.println("--------------------------------------------------");
  printChipInfo();
  Serial.println("--------------------------------------------------");
  printMemoryInfo();
  Serial.println("--------------------------------------------------");
  printHelp();
  Serial.println("==================================================");
}

void handleSerialCommand(char command) {
  switch (command) {
    case '\r':
    case '\n':
      return;
    case 's':
    case 'S':
      printSnapshot("Manual snapshot");
      return;
    case 'r':
    case 'R':
      Serial.println("Triggering ESP.restart() in 500 ms...");
      Serial.flush();
      delay(500);
      ESP.restart();
      return;
    case 'p':
    case 'P':
      Serial.println("Triggering abort() in 500 ms...");
      Serial.flush();
      delay(500);
      abort();
      return;
    case 'd':
    case 'D':
      Serial.println("Entering deep sleep for 3 seconds...");
      Serial.flush();
      delay(200);
      esp_sleep_enable_timer_wakeup(3ULL * 1000ULL * 1000ULL);
      esp_deep_sleep_start();
      return;
    case 'h':
    case 'H':
      printHelp();
      return;
    default:
      Serial.printf("Unknown command '%c'. Send 'h' for help.\r\n", command);
      return;
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1500);

  if (rtc_magic != kRtcMagic) {
    rtc_magic = kRtcMagic;
    boot_count = 0;
    previous_uptime_ms = 0;
    previous_alive_seq = 0;
  }

  boot_count++;
  printSnapshot("FollowBox reset probe boot");
}

void loop() {
  while (Serial.available() > 0) {
    handleSerialCommand(static_cast<char>(Serial.read()));
  }

  const uint32_t now = millis();
  if (now - last_print_ms >= kHeartbeatPeriodMs) {
    last_print_ms = now;
    alive_seq++;
    previous_uptime_ms = now;
    previous_alive_seq = alive_seq;

    Serial.printf("ALIVE seq=%lu uptime=%lu ms heap=%lu min_heap=%lu max_alloc=%lu psram_free=%lu stack=%lu\r\n",
                  static_cast<unsigned long>(alive_seq),
                  static_cast<unsigned long>(now),
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getMinFreeHeap()),
                  static_cast<unsigned long>(ESP.getMaxAllocHeap()),
                  static_cast<unsigned long>(
                      heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                  static_cast<unsigned long>(
                      uxTaskGetStackHighWaterMark(nullptr) * sizeof(StackType_t)));
  }

  delay(10);
}
