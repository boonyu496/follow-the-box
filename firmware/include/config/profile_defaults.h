#pragma once

#include <cstddef>
#include <cstdint>

namespace followbox::profile {

constexpr float REMOTE_MAX_SPEED_SCALE = 0.20f;
constexpr float H5_MAX_SPEED_SCALE = 0.10f;
constexpr float CLOUD_MAX_SPEED_SCALE = 0.08f;
constexpr float AUTO_FOLLOW_MAX_SPEED_SCALE = 0.30f;

constexpr uint32_t PHYSICAL_REMOTE_LOST_STOP_MS = 500;
constexpr uint32_t H5_LOST_STOP_MS = 500;
constexpr uint32_t CLOUD_LOST_STOP_MS = 700;
constexpr uint32_t UWB_STALE_STOP_MS = 1000;
constexpr uint32_t OBSTACLE_STALE_TIMEOUT_MS = 300;
constexpr uint32_t TASK_HEARTBEAT_TIMEOUT_MS = 200;

constexpr int OBSTACLE_STOP_DISTANCE_MM = 500;
constexpr int OBSTACLE_SLOW_DISTANCE_MM = 1000;

constexpr uint32_t THROTTLE_PWM_FREQUENCY_HZ = 1000;
constexpr uint8_t THROTTLE_PWM_RESOLUTION_BITS = 12;
constexpr int THROTTLE_DEADBAND_MV = 800;
constexpr int THROTTLE_MIN_ACTIVE_MV = 1000;
constexpr int THROTTLE_MAX_MV = 3600;
constexpr int THROTTLE_MODULE_FULL_SCALE_MV = 5000;
constexpr float THROTTLE_SLEW_RISE_PER_S = 0.25f;
constexpr float THROTTLE_SLEW_FALL_PER_S = 0.50f;

constexpr int BATTERY_DIVIDER_TOP_OHM = 220000;
constexpr int BATTERY_DIVIDER_BOTTOM_OHM = 10000;
constexpr int BATTERY_SUPPORTED_PACK_MAX_MV = 60000;
constexpr float BATTERY_NOMINAL_VOLTAGE = 36.0f;
constexpr float BATTERY_FULL_VOLTAGE = 42.0f;
constexpr float BATTERY_LOW_VOLTAGE = 33.0f;

// --- Sensor task UART ingestion ---
// Bound bytes drained per parser per update so the control loop never stalls
// on a flooded UART; leftover bytes are picked up next tick.
constexpr uint32_t UWB_UART_BAUD = 115200;
// Current bench capture: the fitted lidar produces stable 55 AA 03 LSN frames
// on the spec wiring at 115200 8N1 after A5 60. 150000 remains in the probe
// list for canonical YDLIDAR AA 55 firmware variants.
constexpr uint32_t LIDAR_UART_BAUD = 115200;
// 115200 8N1 can deliver about 230 bytes per 20 ms sensor tick. The ESP32
// default HardwareSerial RX buffer is too small for jittery ticks, so reserve
// enough ring buffer and drain budget to avoid losing 55AA framing bytes.
constexpr size_t SENSOR_UART_RX_BUFFER_SIZE = 2048;
constexpr int SENSOR_TASK_MAX_BYTES_PER_UPDATE = 2048;

// --- UWB GC-P2304 parser / filter ---
// GC-P2304-GS-2: UART 115200 8N1, binary frame F0 06 ... AA (see protocols/UWB-GC-P2304.md).
constexpr uint32_t UWB_PARSER_TIMEOUT_MS = 1000;
constexpr int UWB_MIN_VALID_DISTANCE_CM = 1;
constexpr int UWB_MAX_VALID_DISTANCE_CM = 5000;
constexpr float UWB_DISTANCE_EMA_ALPHA = 0.40f;
constexpr float UWB_BEARING_EMA_ALPHA_FAST = 0.60f;
constexpr float UWB_BEARING_EMA_ALPHA_SLOW = 0.20f;
constexpr float UWB_BEARING_FAST_THRESHOLD_DEG = 15.0f;
// RSSI (dBm) -> confidence 0..255 mapping window.
constexpr int UWB_RSSI_MIN_DBM = -95;
constexpr int UWB_RSSI_MAX_DBM = -45;

// --- UWB follow controller (outputs MotionIntent only, never GPIO) ---
// Handheld tag height above the lidar/UWB plane; used to flatten 3D range. NEEDS FIELD CALIBRATION.
constexpr float FOLLOW_TAG_HEIGHT_MM = 1000.0f;
constexpr int FOLLOW_NEAR_STOP_DISTANCE_MM = 800;    // stop band (hysteresis low)
constexpr int FOLLOW_NEAR_RESUME_DISTANCE_MM = 950;  // resume band (hysteresis high)
constexpr int FOLLOW_FAR_FULL_SPEED_DISTANCE_MM = 2500;
constexpr float FOLLOW_TURN_FULL_SCALE_DEG = 45.0f;  // bearing mapped to full turn
constexpr float FOLLOW_BEARING_SLOW_DEG = 25.0f;     // above this, forward is reduced
constexpr float FOLLOW_YAW_DAMP_GAIN = 0.0f;         // IMU yaw-rate damping, default off (conservative)
constexpr float FOLLOW_MAX_FORWARD = 1.0f;           // pre-scale; AUTO_FOLLOW_MAX_SPEED_SCALE applies downstream

// --- JY61P IMU (WitMotion 0x55 frames: 0x51 accel / 0x52 gyro / 0x53 angle) ---
// Default baud is a common WitMotion factory value; the actual rate MUST be
// confirmed with a serial sniff before trusting yaw (JY61P.md). RX-only: the
// firmware never reconfigures the module.
constexpr uint32_t IMU_UART_BAUD = 9600;
constexpr uint32_t IMU_PARSER_TIMEOUT_MS = 500;
// WitMotion full-scale ranges for the int16 fields (datasheet defaults).
constexpr float IMU_GYRO_FULL_SCALE_DPS = 2000.0f;  // 0x52 angular rate
constexpr float IMU_ANGLE_FULL_SCALE_DEG = 180.0f;  // 0x53 Euler angles
// yaw sign maps module yaw-rate to the robot turn convention; confirmed in the
// install wizard. Default +1 is conservative (damping stays off until the IMU
// is enabled anyway, FOLLOW_YAW_DAMP_GAIN = 0).
constexpr float IMU_YAW_SIGN = 1.0f;

// --- Fitted OEM lidar (current 115200 55AA field frames; 150000 AA55 fallback) ---
constexpr uint32_t LIDAR_PACKET_TIMEOUT_MS = 500;
constexpr int LIDAR_MIN_VALID_MM = 50;
constexpr int LIDAR_MAX_VALID_MM = 8000;
// Mounting yaw offset between lidar 0deg and robot forward. NEEDS PHYSICAL VERIFICATION.
constexpr float LIDAR_MOUNT_YAW_OFFSET_DEG = 0.0f;
constexpr float LIDAR_FRONT_CENTER_HALF_DEG = 15.0f;
constexpr float LIDAR_FRONT_SIDE_HALF_DEG = 45.0f;
constexpr float LIDAR_SIDE_CENTER_DEG = 90.0f;
constexpr float LIDAR_SIDE_HALF_DEG = 30.0f;

// --- Forward TOF: TCA9548A + 3x VL53L1X (I2C GPIO10/11, 3V3, 4.7k pull-ups) ---
// First prototype has no XSHUT wired, so the bus must be cleared on init/recovery
// (see CURRENT-WIRING-AI.md). Channel map follows ASSEMBLY-WIRING-MINDMAP:
// SD0 -> front-center, SD1 -> front-left, SD2 -> front-right.
constexpr uint8_t TOF_TCA9548A_ADDR = 0x70;
constexpr uint8_t TOF_CHANNEL_FRONT_CENTER = 0;
constexpr uint8_t TOF_CHANNEL_FRONT_LEFT = 1;
constexpr uint8_t TOF_CHANNEL_FRONT_RIGHT = 2;
constexpr uint32_t TOF_I2C_CLOCK_HZ = 100000;        // safer for prototype wiring
constexpr uint32_t TOF_TIMING_BUDGET_US = 33000;     // 33 ms Medium-mode budget
constexpr uint32_t TOF_CONTINUOUS_PERIOD_MS = 33;    // inter-measurement period
constexpr uint32_t TOF_STALE_TIMEOUT_MS = 300;       // matches OBSTACLE_STALE_TIMEOUT_MS
constexpr uint32_t TOF_REINIT_INTERVAL_MS = 1000;    // retry one failed mux channel per second
constexpr uint8_t TOF_FAILURES_BEFORE_BUS_CLEAR = 3; // repeated NACK/timeout recovery threshold
constexpr int TOF_MIN_VALID_MM = 40;                 // below this = no target / cross-talk
constexpr int TOF_MAX_VALID_MM = 4000;               // reject impossible/overflow readings
constexpr int TOF_LOG_DELTA_MM = 30;                 // debug log when range changes this much

// --- Side ultrasonic: two HC-SR04 sharing TRIG GPIO9, Echo GPIO40/41 (divided) ---
// Both fire together; the echoes are captured non-blocking via pin interrupts so
// the sensor task never stalls on pulseIn. Echo lines MUST be level-shifted to
// 3.3V (CURRENT-WIRING-AI.md).
constexpr uint32_t ULTRASONIC_CYCLE_PERIOD_MS = 60;    // >= max echo window + margin
constexpr uint32_t ULTRASONIC_ECHO_TIMEOUT_US = 25000; // ~4.2 m round trip ceiling
constexpr uint32_t ULTRASONIC_STALE_TIMEOUT_MS = 300;
constexpr int ULTRASONIC_MIN_VALID_MM = 20;            // HC-SR04 blind zone
constexpr int ULTRASONIC_MAX_VALID_MM = 4000;          // datasheet usable ceiling

// --- ESP32-S3-CAM video link (status only, never a safety input) ---
// The link is considered online while heartbeats keep arriving from the web/comm
// layer; staleness only flips an H5 indicator and never gates motion.
constexpr uint32_t CAMERA_LINK_STALE_TIMEOUT_MS = 3000;

}  // namespace followbox::profile
