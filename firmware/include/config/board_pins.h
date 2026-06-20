#pragma once

namespace followbox::pins {

constexpr int PIN_RC_CH1_STEERING = 4;
constexpr int PIN_RC_CH2_THROTTLE = 5;
constexpr int PIN_RC_CH3_SPEED = 6;
constexpr int PIN_RC_CH4_MODE = 7;
constexpr int PIN_RC_CH5_STOP = 8;
constexpr int PIN_RC_CH6_AUX = -1;

constexpr int PIN_US_SHARED_TRIG = 9;

constexpr int PIN_I2C_SDA = 10;
constexpr int PIN_I2C_SCL = 11;

constexpr int PIN_LEFT_THROTTLE_PWM = 12;
constexpr int PIN_RIGHT_THROTTLE_PWM = 13;
constexpr int PIN_BRAKE_OUT = 14;
constexpr int PIN_LEFT_REVERSE_OUT = 15;
constexpr int PIN_RIGHT_REVERSE_OUT = 16;
constexpr int PIN_DRIVE_ENABLE_OUT = 39;

constexpr int PIN_UWB_TX = 17;
constexpr int PIN_UWB_RX = 18;

// UART resource assignment (ESP32-S3 has UART0 reserved for USB debug).
constexpr int UART_NUM_UWB = 1;
constexpr int UART_NUM_LIDAR = 2;
// JY61P IMU: ESP32-S3 has 3 UART controllers. The debug log runs over native
// USB-CDC (ARDUINO_USB_CDC_ON_BOOT=1), NOT UART0, so the UART0 *controller* is
// free even though UART1=UWB and UART2=lidar are taken. The chosen enable path
// is to remap free UART0 onto GPIO42 (PIN_IMU_RX, a legal non-boot pin) - a real
// hardware UART with no contention against the UWB/lidar streams, preferred over
// SoftwareSerial (lossy beside 230400 lidar) or sharing UART2 (lidar must stay
// always-on). Kept -1 (disabled, imu stays invalid, yaw damping off) until that
// bring-up: set UART_NUM_IMU=0, add a Serial0 case in UartBus, and confirm the
// JY61P TX 5V -> GPIO42 level shift. Bench debug must not rely on UART0/GPIO43-44.
constexpr int UART_NUM_IMU = -1;

// EAI S2 lidar UART (UART2, 150000 baud for S2-YJ/S2-YD, 3.3V logic, direct connect).
// GPIO3  = ESP32 RX <- lidar TX.  GPIO22/23 do NOT exist on ESP32-S3-DevKitC-1.
// GPIO43 = ESP32 TX -> lidar RX (motor-speed control).  Safe because ARDUINO_USB_CDC_ON_BOOT=1
// routes debug over native USB (GPIO19/20 internal), leaving UART0/GPIO43 free; the on-board
// CH340 is a high-impedance RX and does not conflict.  Do NOT connect the UART USB port
// simultaneously when GPIO43 is wired to lidar.
// GPIO19/20 are USB D-/D+ and MUST NOT be used.
constexpr int PIN_LIDAR_RX = 3;
constexpr int PIN_LIDAR_TX = 43;

constexpr int PIN_ESTOP_STATUS = 21;
constexpr int PIN_BATTERY_ADC = 1;
constexpr int PIN_CONTROLLER_FAULT = 2;

constexpr int PIN_US_LEFT_ECHO = 40;
constexpr int PIN_US_RIGHT_ECHO = 41;
constexpr int PIN_IMU_RX = 42;
constexpr int PIN_IMU_TX = -1;
constexpr int PIN_BUZZER = -1;

constexpr int DISALLOWED_MOTOR_GPIO_35 = 35;
constexpr int DISALLOWED_MOTOR_GPIO_36 = 36;
constexpr int DISALLOWED_MOTOR_GPIO_37 = 37;
constexpr int DISALLOWED_MOTOR_GPIO_47 = 47;
constexpr int DISALLOWED_MOTOR_GPIO_48 = 48;

}  // namespace followbox::pins

