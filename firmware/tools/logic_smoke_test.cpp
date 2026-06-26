#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "app/app.h"
#include "app/command_pipeline.h"
#include "app/mode_manager.h"
#include "control/follow_controller_uwb.h"
#include "control/motion_mixer.h"
#include "control/obstacle_manager.h"
#include "core/system_state.h"
#include "core/time_utils.h"
#include "safety/safety_manager.h"
#include "sensors/jy61p_imu.h"
#include "sensors/lidar_eai_s2.h"
#include "sensors/obstacle_fusion.h"
#include "sensors/uwb_gc_p2304.h"
#include "web/h5_command_handler.h"
#include "web/h5_request_parser.h"
#include "web/telemetry_api.h"

using namespace followbox;

namespace {

void testTimeUtils() {
  assert(!isStale(1000, 1001, 100));
  assert(elapsedMsClamped(1000, 1001) == 0);

  assert(isStale(1000, 1100, 100));
  assert(elapsedMsClamped(1000, 1100) > 1000);

  assert(!isStale(5, 0xFFFFFFFEu, 100));
  assert(elapsedMsClamped(5, 0xFFFFFFFEu) == 7);
}

// --- Existing safety / mode / mixer regression checks ---------------------
void testSafetyAndMixer() {
  SafetyManager safety;
  ModeManager modes;

  SystemState state;
  state.now_ms = 1000;
  state.estop_active = false;
  state.install_wizard_complete = true;
  state.mode = RunMode::MANUAL_RC;
  state.rc.online = true;
  state.rc.last_update_ms = 980;
  state.rc.throttle = 0.5f;
  state.rc.steering = 0.0f;

  state.safety = safety.evaluate(state);
  assert(state.safety.motion_allowed);
  assert(state.safety.stop_reason == StopReason::NONE);
  assert(modes.selectMode(state, state.safety) == RunMode::MANUAL_RC);

  MotionMixer mixer;
  MotionIntent intent;
  intent.source = ControlSource::DS600_RC;
  intent.request_motion = true;
  intent.forward = 0.5f;
  intent.turn = 0.5f;
  MotorCommand mixed = mixer.mix(intent, 1.0f, 1100);
  assert(mixed.enable);
  assert(!mixed.brake);
  assert(mixed.left_target >= mixed.right_target);

  SafetyManager estop_safety;
  state.estop_active = true;
  state.safety = estop_safety.evaluate(state);
  MotorCommand gated = estop_safety.applyFinalGate(mixed, state);
  assert(!gated.enable);
  assert(gated.brake);
  assert(std::fabs(gated.left_target) < 0.0001f);
  assert(std::fabs(gated.right_target) < 0.0001f);

  SafetyManager rc_loss_safety;
  state = SystemState{};
  state.now_ms = 5000;
  state.estop_active = false;
  state.mode = RunMode::MANUAL_RC;
  state.heartbeat.sensor_task_ms = 4990;
  state.heartbeat.uwb_task_ms = 4990;
  state.rc.online = false;
  state.rc.last_update_ms = 0;
  state.safety = rc_loss_safety.evaluate(state);
  assert(!state.safety.motion_allowed);
  assert(!state.safety.fault_latched);
  assert(state.safety.stop_reason == StopReason::RC_LOST);

  state.rc.online = true;
  state.rc.last_update_ms = 5000;
  state.safety = rc_loss_safety.evaluate(state);
  assert(state.safety.motion_allowed);
  assert(!state.safety.fault_latched);
  assert(state.safety.stop_reason == StopReason::NONE);

  SafetyManager auto_safety;
  state = SystemState{};
  state.now_ms = 2000;
  state.estop_active = false;
  state.mode = RunMode::AUTO_FOLLOW;
  state.install_wizard_complete = true;
  state.throttle_calibrated = true;
  state.heartbeat.sensor_task_ms = 2000;
  state.heartbeat.uwb_task_ms = 2000;
  state.obstacle.valid = true;
  state.obstacle.last_update_ms = 2000;
  state.obstacle.front_center_mm = 1500;
  state.uwb.valid = false;
  state.safety = auto_safety.evaluate(state);
  assert(!state.safety.motion_allowed);
  assert(state.safety.stop_reason == StopReason::UWB_LOST);

  // Side-only ultrasonic data must not satisfy AUTO's forward-obstacle gate.
  state.uwb.valid = true;
  state.uwb.last_update_ms = state.now_ms;
  state.uwb.distance_mm = 2000;
  state.obstacle = ObstacleSnapshot{};
  state.obstacle.valid = true;
  state.obstacle.last_update_ms = state.now_ms;
  state.obstacle.side_left_mm = 600;
  state.safety = auto_safety.evaluate(state);
  assert(!state.safety.motion_allowed);
  assert(state.safety.stop_reason == StopReason::SENSOR_TIMEOUT);

  SafetyManager cloud_safety;
  state = SystemState{};
  state.now_ms = 9000;
  state.estop_active = false;
  state.mode = RunMode::MANUAL_CLOUD_LOW_SPEED;
  state.heartbeat.sensor_task_ms = 8990;
  state.heartbeat.uwb_task_ms = 8990;
  state.cloud.connected = true;
  state.cloud.last_update_ms = 8990;
  state.cloud.unlock_request = true;
  state.safety = cloud_safety.evaluate(state);
  assert(state.safety.motion_allowed);
  assert(state.safety.stop_reason == StopReason::NONE);

  state.cloud.last_update_ms = 8000;
  state.safety = cloud_safety.evaluate(state);
  assert(!state.safety.motion_allowed);
  assert(state.safety.stop_reason == StopReason::CLOUD_LOST);
}

// --- UWB GC-P2304 parser --------------------------------------------------
void feedUwb(UwbGcP2304Parser& parser, const std::vector<uint8_t>& bytes,
             uint32_t now_ms, bool expect_frame) {
  bool got = false;
  for (uint8_t b : bytes) {
    got = parser.pushByte(b, now_ms) || got;
  }
  assert(got == expect_frame);
}

void testUwbParser() {
  UwbGcP2304Parser parser;
  parser.reset();

  // Spec sample: ID 0x0003, 115 cm, 20 deg, RSSI 0xBC -> -68 dBm.
  feedUwb(parser, {0xF0, 0x06, 0x03, 0x00, 0x73, 0x00, 0x14, 0x00, 0xBC, 0xAA},
          1000, true);
  const UwbTarget& t = parser.target();
  assert(t.valid);
  assert(t.distance_mm == 1150);
  assert(std::fabs(t.bearing_deg - 20.0f) < 0.001f);
  assert(t.confidence > 0);
  assert(parser.stats().frame_count == 1);
  assert(parser.stats().last_rssi_dbm == -68);

  // Bad tail -> parse error, target stays from the previous good frame.
  feedUwb(parser, {0xF0, 0x06, 0x03, 0x00, 0x73, 0x00, 0x14, 0x00, 0xBC, 0x00},
          1010, false);
  assert(parser.stats().parse_error_count == 1);

  // Re-sync after garbage leading bytes (second frame: 0x0064 = 100 cm).
  // Reset first so the EMA initialises directly to the new sample.
  parser.reset();
  feedUwb(parser, {0x11, 0x22, 0xF0, 0x06, 0x03, 0x00, 0x64, 0x00, 0xF6, 0x00,
                   0xC0, 0xAA},
          1020, true);
  assert(parser.target().distance_mm == 1000);

  // Timeout invalidates the target and clears the filter.
  parser.update(1020 + 1001);
  assert(!parser.target().valid);
  assert(parser.target().confidence == 0);
}

// --- UWB follow controller ------------------------------------------------
UwbTarget makeTarget(int distance_mm, float bearing_deg) {
  UwbTarget t;
  t.valid = true;
  t.last_update_ms = 1;
  t.distance_mm = distance_mm;
  t.bearing_deg = bearing_deg;
  t.confidence = 200;
  return t;
}

void testFollowController() {
  FollowControllerUwb controller;
  ImuSnapshot imu;  // invalid -> no yaw damping

  // Invalid target -> no motion request.
  controller.reset();
  MotionIntent idle = controller.update(UwbTarget{}, imu, 10);
  assert(!idle.request_motion);
  assert(std::fabs(idle.forward) < 0.0001f);

  // Near target (flat ~568 mm < 800) -> stop band, but still turns.
  controller.reset();
  MotionIntent near = controller.update(makeTarget(1150, 20.0f), imu, 20);
  assert(near.request_motion);
  assert(std::fabs(near.forward) < 0.0001f);
  assert(near.turn > 0.0f);  // bearing +20 -> turn toward tag

  // Far target -> full forward, small bearing -> no slow-down.
  controller.reset();
  MotionIntent far = controller.update(makeTarget(3000, 5.0f), imu, 30);
  assert(far.forward > 0.9f);
  assert(far.turn > 0.0f);

  // Large bearing reduces forward (turn-first behaviour).
  controller.reset();
  MotionIntent skew = controller.update(makeTarget(3000, 35.0f), imu, 40);
  assert(skew.forward < far.forward);
  assert(skew.forward > 0.0f);

  // Negative bearing -> turn the other way.
  controller.reset();
  MotionIntent right = controller.update(makeTarget(3000, -30.0f), imu, 50);
  assert(right.turn < 0.0f);

  // Hysteresis: once stopped near, stay stopped while inside the dead band.
  controller.reset();
  controller.update(makeTarget(1150, 0.0f), imu, 60);  // flat ~568 -> latch
  MotionIntent band =
      controller.update(makeTarget(1345, 0.0f), imu, 70);  // flat ~900
  assert(std::fabs(band.forward) < 0.0001f);  // still latched (resume=950)
  MotionIntent release = controller.update(makeTarget(3000, 0.0f), imu, 80);
  assert(release.forward > 0.0f);  // beyond resume -> moves again
}

// --- Fitted YDLIDAR/55AA LiDAR parser -------------------------------------
uint8_t lidarCrc(const uint8_t* data, uint8_t length) {
  static const uint8_t table[256] = {
      0x00, 0x4d, 0x9a, 0xd7, 0x79, 0x34, 0xe3, 0xae, 0xf2, 0xbf, 0x68, 0x25,
      0x8b, 0xc6, 0x11, 0x5c, 0xa9, 0xe4, 0x33, 0x7e, 0xd0, 0x9d, 0x4a, 0x07,
      0x5b, 0x16, 0xc1, 0x8c, 0x22, 0x6f, 0xb8, 0xf5, 0x1f, 0x52, 0x85, 0xc8,
      0x66, 0x2b, 0xfc, 0xb1, 0xed, 0xa0, 0x77, 0x3a, 0x94, 0xd9, 0x0e, 0x43,
      0xb6, 0xfb, 0x2c, 0x61, 0xcf, 0x82, 0x55, 0x18, 0x44, 0x09, 0xde, 0x93,
      0x3d, 0x70, 0xa7, 0xea, 0x3e, 0x73, 0xa4, 0xe9, 0x47, 0x0a, 0xdd, 0x90,
      0xcc, 0x81, 0x56, 0x1b, 0xb5, 0xf8, 0x2f, 0x62, 0x97, 0xda, 0x0d, 0x40,
      0xee, 0xa3, 0x74, 0x39, 0x65, 0x28, 0xff, 0xb2, 0x1c, 0x51, 0x86, 0xcb,
      0x21, 0x6c, 0xbb, 0xf6, 0x58, 0x15, 0xc2, 0x8f, 0xd3, 0x9e, 0x49, 0x04,
      0xaa, 0xe7, 0x30, 0x7d, 0x88, 0xc5, 0x12, 0x5f, 0xf1, 0xbc, 0x6b, 0x26,
      0x7a, 0x37, 0xe0, 0xad, 0x03, 0x4e, 0x99, 0xd4, 0x7c, 0x31, 0xe6, 0xab,
      0x05, 0x48, 0x9f, 0xd2, 0x8e, 0xc3, 0x14, 0x59, 0xf7, 0xba, 0x6d, 0x20,
      0xd5, 0x98, 0x4f, 0x02, 0xac, 0xe1, 0x36, 0x7b, 0x27, 0x6a, 0xbd, 0xf0,
      0x5e, 0x13, 0xc4, 0x89, 0x63, 0x2e, 0xf9, 0xb4, 0x1a, 0x57, 0x80, 0xcd,
      0x91, 0xdc, 0x0b, 0x46, 0xe8, 0xa5, 0x72, 0x3f, 0xca, 0x87, 0x50, 0x1d,
      0xb3, 0xfe, 0x29, 0x64, 0x38, 0x75, 0xa2, 0xef, 0x41, 0x0c, 0xdb, 0x96,
      0x42, 0x0f, 0xd8, 0x95, 0x3b, 0x76, 0xa1, 0xec, 0xb0, 0xfd, 0x2a, 0x67,
      0xc9, 0x84, 0x53, 0x1e, 0xeb, 0xa6, 0x71, 0x3c, 0x92, 0xdf, 0x08, 0x45,
      0x19, 0x54, 0x83, 0xce, 0x60, 0x2d, 0xfa, 0xb7, 0x5d, 0x10, 0xc7, 0x8a,
      0x24, 0x69, 0xbe, 0xf3, 0xaf, 0xe2, 0x35, 0x78, 0xd6, 0x9b, 0x4c, 0x01,
      0xa8, 0xe5, 0x32, 0x7f, 0xd1, 0x9c, 0x4b, 0x06, 0x5a, 0x17, 0xc0, 0x8d,
      0x23, 0x6e, 0xb9, 0xf4};
  uint8_t crc = 0;
  for (uint8_t i = 0; i < length; ++i) {
    crc = table[(crc ^ data[i]) & 0xff];
  }
  return crc;
}

std::vector<uint8_t> buildLidarPacket(float start_deg, float end_deg,
                                      uint16_t dist_mm, uint8_t ring_start) {
  constexpr uint8_t count = 8;
  std::vector<uint8_t> p(10 + count * 3, 0);
  p[0] = 0xAA;
  p[1] = 0x55;
  p[2] = ring_start ? 0x01 : 0x00;
  p[3] = count;
  const uint16_t start =
      (static_cast<uint16_t>(std::lround(start_deg * 64.0f)) << 1) | 1u;
  const uint16_t end =
      (static_cast<uint16_t>(std::lround(end_deg * 64.0f)) << 1) | 1u;
  p[4] = start & 0xFF;
  p[5] = (start >> 8) & 0xFF;
  p[6] = end & 0xFF;
  p[7] = (end >> 8) & 0xFF;
  uint16_t checksum = 0x55AA ^ static_cast<uint16_t>(count << 8 | p[2]) ^
                      start ^ end;
  const uint16_t raw_distance = static_cast<uint16_t>(dist_mm * 4u);
  for (uint8_t i = 0; i < count; ++i) {
    const size_t offset = 10 + i * 3;
    const uint8_t quality = static_cast<uint8_t>(0x80u + i);
    p[offset] = quality;
    p[offset + 1] = raw_distance & 0xFF;
    p[offset + 2] = (raw_distance >> 8) & 0xFF;
    checksum ^= quality;
    checksum ^= raw_distance;
  }
  p[8] = checksum & 0xFF;
  p[9] = (checksum >> 8) & 0xFF;
  return p;
}

std::vector<uint8_t> buildLidarNoChecksumPacket(float start_deg, float end_deg,
                                                uint16_t dist_mm,
                                                uint8_t ring_start) {
  constexpr uint8_t count = 8;
  std::vector<uint8_t> p(8 + count * 3, 0);
  p[0] = 0xAA;
  p[1] = 0x55;
  p[2] = ring_start ? 0x01 : 0x00;
  p[3] = count;
  const uint16_t start =
      (static_cast<uint16_t>(std::lround(start_deg * 64.0f)) << 1) | 1u;
  const uint16_t end =
      (static_cast<uint16_t>(std::lround(end_deg * 64.0f)) << 1) | 1u;
  p[4] = start & 0xFF;
  p[5] = (start >> 8) & 0xFF;
  p[6] = end & 0xFF;
  p[7] = (end >> 8) & 0xFF;
  const uint16_t raw_distance = static_cast<uint16_t>(dist_mm * 4u);
  for (uint8_t i = 0; i < count; ++i) {
    const size_t offset = 8 + i * 3;
    p[offset] = raw_distance & 0xFF;
    p[offset + 1] = (raw_distance >> 8) & 0xFF;
    p[offset + 2] = static_cast<uint8_t>(0x80u + i);
  }
  return p;
}

std::vector<uint8_t> buildLidarNoIntensityPacket(float start_deg, float end_deg,
                                                 uint16_t dist_mm,
                                                 uint8_t ring_start) {
  constexpr uint8_t count = 8;
  std::vector<uint8_t> p(10 + count * 2, 0);
  p[0] = 0xAA;
  p[1] = 0x55;
  p[2] = ring_start ? 0x01 : 0x00;
  p[3] = count;
  const uint16_t start =
      (static_cast<uint16_t>(std::lround(start_deg * 64.0f)) << 1) | 1u;
  const uint16_t end =
      (static_cast<uint16_t>(std::lround(end_deg * 64.0f)) << 1) | 1u;
  p[4] = start & 0xFF;
  p[5] = (start >> 8) & 0xFF;
  p[6] = end & 0xFF;
  p[7] = (end >> 8) & 0xFF;
  uint16_t checksum = 0x55AA ^ static_cast<uint16_t>(count << 8 | p[2]) ^
                      start ^ end;
  const uint16_t raw_distance = static_cast<uint16_t>(dist_mm * 4u);
  for (uint8_t i = 0; i < count; ++i) {
    const size_t offset = 10 + i * 2;
    p[offset] = raw_distance & 0xFF;
    p[offset + 1] = (raw_distance >> 8) & 0xFF;
    checksum ^= raw_distance;
  }
  p[8] = checksum & 0xFF;
  p[9] = (checksum >> 8) & 0xFF;
  return p;
}

std::vector<uint8_t> buildLidarHeader55aaPacket(float start_deg, float end_deg,
                                                uint16_t dist_mm) {
  constexpr uint8_t count = 8;
  std::vector<uint8_t> p(8 + count * 3, 0);
  p[0] = 0x55;
  p[1] = 0xAA;
  p[2] = 0x03;
  p[3] = count;
  const uint16_t start =
      static_cast<uint16_t>(std::lround(start_deg * 64.0f)) << 1;
  const uint16_t end =
      static_cast<uint16_t>(std::lround(end_deg * 64.0f)) << 1;
  p[4] = start & 0xFF;
  p[5] = (start >> 8) & 0xFF;
  p[6] = end & 0xFF;
  p[7] = (end >> 8) & 0xFF;
  const uint16_t raw_distance = static_cast<uint16_t>(dist_mm * 4u);
  for (uint8_t i = 0; i < count; ++i) {
    const size_t offset = 8 + i * 3;
    p[offset] = raw_distance & 0xFF;
    p[offset + 1] = (raw_distance >> 8) & 0xFF;
    p[offset + 2] = static_cast<uint8_t>(0xB0u + i);
  }
  return p;
}

void feedLidar(LidarEaiS2& lidar, const std::vector<uint8_t>& packet,
               uint32_t now_ms) {
  for (uint8_t b : packet) {
    lidar.pushByte(b, now_ms);
  }
}

void testLidarParser() {
  LidarEaiS2 lidar;
  lidar.reset();

  // A ring-start begins scan A; the next ring-start finalises it.
  feedLidar(lidar, buildLidarPacket(10.0f, 20.0f, 1500, true), 100);
  feedLidar(lidar, buildLidarPacket(30.0f, 40.0f, 1800, false), 105);
  feedLidar(lidar, buildLidarPacket(100.0f, 110.0f, 800, true), 110);

  assert(lidar.stats().packet_count == 3);
  assert(lidar.stats().checksum_error_count == 0);
  assert(lidar.snapshot().valid);
  assert(lidar.snapshot().front_center_mm == 1500);
  assert(lidar.snapshot().side_left_mm == 0);

  // A corrupted checksum is rejected so noise cannot create obstacle data.
  std::vector<uint8_t> bad = buildLidarPacket(50.0f, 60.0f, 1000, false);
  bad[8] ^= 0xFF;
  feedLidar(lidar, bad, 120);
  assert(lidar.stats().packet_count == 3);
  assert(lidar.stats().checksum_error_count == 1);

  // Timeout invalidates the snapshot.
  lidar.update(120 + 600);
  assert(!lidar.snapshot().valid);

  // Buyer ROS drivers omit CS and use distance_lsb + distance_msb + quality.
  // The firmware accepts that layout only after seeing the next AA55 header.
  LidarEaiS2 no_cs;
  no_cs.reset();
  std::vector<uint8_t> a = buildLidarNoChecksumPacket(350.0f, 355.0f, 155, false);
  std::vector<uint8_t> b = buildLidarNoChecksumPacket(0.0f, 10.0f, 155, false);
  std::vector<uint8_t> c = buildLidarNoChecksumPacket(20.0f, 30.0f, 200, false);
  std::vector<uint8_t> stream;
  stream.insert(stream.end(), a.begin(), a.end());
  stream.insert(stream.end(), b.begin(), b.end());
  stream.push_back(c[0]);
  stream.push_back(c[1]);
  feedLidar(no_cs, stream, 200);
  assert(no_cs.stats().packet_count == 2);
  assert(no_cs.stats().no_checksum_packet_count == 2);
  assert(no_cs.stats().checksum_error_count == 0);
  assert(no_cs.stats().scan_count == 1);
  assert(no_cs.snapshot().valid);

  // NODE_QUAL0 mode carries two bytes per sample plus the normal check code.
  LidarEaiS2 no_intensity;
  no_intensity.reset();
  feedLidar(no_intensity,
            buildLidarNoIntensityPacket(350.0f, 355.0f, 600, true), 205);
  feedLidar(no_intensity,
            buildLidarNoIntensityPacket(0.0f, 10.0f, 700, true), 210);
  assert(no_intensity.stats().packet_count == 2);
  assert(no_intensity.stats().no_intensity_packet_count == 2);
  assert(no_intensity.stats().checksum_error_count == 0);
  assert(no_intensity.snapshot().valid);

  // 2026-06-24 bench logs show the fitted unit on spec wiring at 115200 8N1
  // emitting 55 AA 03 08 packets after A5 60. Accept only the plausible
  // distance-first layout; wrong-baud captures with impossible angles remain
  // rejected below.
  LidarEaiS2 header_55aa;
  header_55aa.reset();
  feedLidar(header_55aa,
            buildLidarHeader55aaPacket(350.0f, 355.0f, 620), 215);
  feedLidar(header_55aa,
            buildLidarHeader55aaPacket(0.0f, 10.0f, 620), 216);
  feedLidar(header_55aa,
            buildLidarHeader55aaPacket(20.0f, 30.0f, 720), 217);
  assert(header_55aa.stats().packet_count == 3);
  assert(header_55aa.stats().header_55aa_packet_count == 3);
  assert(header_55aa.stats().no_checksum_packet_count == 3);
  assert(header_55aa.snapshot().valid);

  // Wrong-baud field captures can still contain false AA55 syncs before 55AA
  // bytes. Impossible angle fields must keep those diagnostic-only.
  LidarEaiS2 captured;
  captured.reset();
  const std::vector<uint8_t> raw55aa = {
      0xAA, 0x55, 0x55, 0xAA, 0x03, 0x08, 0x95, 0xC9, 0x47, 0xBB, 0x3F, 0x00,
      0xBE, 0x3E, 0x00, 0xDB, 0x35, 0x00, 0xFF, 0x30, 0x00, 0xFF, 0x31, 0x00,
      0xFF, 0x36, 0x00, 0xFF, 0x3B, 0x00, 0xE9, 0x34, 0x00, 0xFF, 0x3C, 0xBF,
      0x83, 0x54, 0x55, 0xAA, 0x03, 0x08, 0x95, 0xC9, 0x89, 0xBF, 0x34, 0x00};
  feedLidar(captured, raw55aa, 210);
  assert(captured.stats().aa55_header_count == 1);
  assert(captured.stats().header_55aa_count == 2);
  assert(captured.stats().packet_count == 0);
  assert(!captured.snapshot().valid);

  // Stable-looking 55AA records with impossible encoded angles must still be
  // rejected rather than accepted merely because the byte stream repeats.
  LidarEaiS2 stable_55aa;
  stable_55aa.reset();
  const std::vector<uint8_t> stable55aa = {
      0x55, 0xAA, 0x03, 0x08, 0x75, 0xCA, 0x1C, 0xDB, 0x1F, 0x06, 0xEA, 0x4B,
      0x06, 0xE8, 0x7E, 0x06, 0xE9, 0xB8, 0x06, 0xE5, 0xD9, 0x06, 0xE5, 0x1D,
      0x07, 0xE3, 0x6D, 0x07, 0xE3, 0xE6, 0x07, 0xD8, 0xFE, 0xDE, 0x66, 0x11,
      0x55, 0xAA, 0x03, 0x08, 0x75, 0xCA, 0x4C, 0xDF, 0x11, 0x08, 0xE6, 0xD3};
  feedLidar(stable_55aa, stable55aa, 220);
  assert(stable_55aa.stats().header_55aa_count == 2);
  assert(stable_55aa.stats().packet_count == 0);
  assert(!stable_55aa.snapshot().valid);
}

// --- JY61P IMU parser (WitMotion 0x55 frames) -----------------------------
// Build an 11-byte WitMotion frame with a valid checksum.
std::vector<uint8_t> buildImuFrame(uint8_t type, int16_t a, int16_t b,
                                   int16_t c, int16_t d) {
  std::vector<uint8_t> f = {
      0x55, type,
      static_cast<uint8_t>(a & 0xFF), static_cast<uint8_t>((a >> 8) & 0xFF),
      static_cast<uint8_t>(b & 0xFF), static_cast<uint8_t>((b >> 8) & 0xFF),
      static_cast<uint8_t>(c & 0xFF), static_cast<uint8_t>((c >> 8) & 0xFF),
      static_cast<uint8_t>(d & 0xFF), static_cast<uint8_t>((d >> 8) & 0xFF)};
  uint8_t sum = 0;
  for (uint8_t byte : f) {
    sum = static_cast<uint8_t>(sum + byte);
  }
  f.push_back(sum);
  return f;
}

void feedImu(Jy61pImu& imu, const std::vector<uint8_t>& bytes, uint32_t now_ms) {
  for (uint8_t byte : bytes) {
    imu.pushByte(byte, now_ms);
  }
}

void testImuParser() {
  Jy61pImu imu;
  imu.reset();

  // Angle frame: roll/pitch/yaw scaled by 180/32768 deg per count.
  feedImu(imu, buildImuFrame(0x53, 16384, -16384, 8192, 0), 100);
  assert(imu.stats().angle_frame_count == 1);
  assert(imu.stats().checksum_error_count == 0);
  assert(imu.snapshot().valid);
  assert(std::fabs(imu.snapshot().roll_deg - 90.0f) < 0.5f);
  assert(std::fabs(imu.snapshot().pitch_deg + 90.0f) < 0.5f);
  assert(std::fabs(imu.snapshot().yaw_deg - 45.0f) < 0.5f);

  // Gyro frame: wz scaled by 2000/32768 dps per count (8192 -> 500 dps).
  feedImu(imu, buildImuFrame(0x52, 0, 0, 8192, 0), 110);
  assert(imu.stats().gyro_frame_count == 1);
  assert(std::fabs(imu.snapshot().yaw_rate_dps - 500.0f) < 1.0f);

  // Corrupted checksum is rejected.
  std::vector<uint8_t> bad = buildImuFrame(0x53, 0, 0, 0, 0);
  bad[10] ^= 0xFF;
  feedImu(imu, bad, 120);
  assert(imu.stats().checksum_error_count == 1);

  // Timeout invalidates and zeroes the yaw rate.
  imu.update(120 + 600);
  assert(!imu.snapshot().valid);
  assert(std::fabs(imu.snapshot().yaw_rate_dps) < 0.0001f);
}

// --- AUTO_FOLLOW pipeline integration -------------------------------------
void testFollowPipeline() {
  CommandPipeline pipeline;
  SystemState state;
  state.mode = RunMode::AUTO_FOLLOW;
  state.now_ms = 500;
  state.uwb = makeTarget(3000, 10.0f);

  MotionIntent intent = pipeline.buildIntent(state);
  assert(intent.source == ControlSource::UWB_FOLLOW);
  assert(intent.request_motion);
  assert(intent.forward > 0.0f);

  // UWB lost -> the pipeline requests no motion (safety still gates anyway).
  state.uwb.valid = false;
  MotionIntent lost = pipeline.buildIntent(state);
  assert(!lost.request_motion);
  assert(std::fabs(lost.forward) < 0.0001f);
}

// --- Obstacle manager P0 (front slow/stop only) ---------------------------
MotionIntent forwardIntent(float forward, float turn) {
  MotionIntent intent;
  intent.source = ControlSource::DS600_RC;
  intent.request_motion = true;
  intent.forward = forward;
  intent.turn = turn;
  return intent;
}

ObstacleSnapshot frontSnapshot(int front_center_mm) {
  ObstacleSnapshot snap;
  snap.valid = true;
  snap.last_update_ms = 1;
  snap.front_left_mm = 0;
  snap.front_center_mm = front_center_mm;
  snap.front_right_mm = 0;
  snap.side_left_mm = 0;
  snap.side_right_mm = 0;
  return snap;
}

void testObstacleManager() {
  ObstacleManager manager;

  // Invalid snapshot -> pass-through (no sensor yet wired).
  ObstacleDecision none =
      manager.apply(forwardIntent(0.8f, 0.2f), ObstacleSnapshot{});
  assert(std::fabs(none.intent.forward - 0.8f) < 0.0001f);
  assert(!none.stop_required);

  // Clear ahead (2000 mm > slow 1000) -> no attenuation.
  ObstacleDecision clear = manager.apply(forwardIntent(0.8f, 0.2f), frontSnapshot(2000));
  assert(std::fabs(clear.intent.forward - 0.8f) < 0.0001f);

  // Inside slow band (750 mm) -> forward scaled down but non-zero, turn kept.
  ObstacleDecision slow = manager.apply(forwardIntent(0.8f, 0.2f), frontSnapshot(750));
  assert(slow.intent.forward > 0.0f);
  assert(slow.intent.forward < 0.8f);
  assert(std::fabs(slow.intent.turn - 0.2f) < 0.0001f);

  // Inside stop band (400 mm < 500) -> forward zeroed, stop flagged.
  ObstacleDecision stop = manager.apply(forwardIntent(0.8f, 0.3f), frontSnapshot(400));
  assert(stop.stop_required);
  assert(std::fabs(stop.intent.forward) < 0.0001f);
  assert(std::fabs(stop.intent.turn - 0.3f) < 0.0001f);  // turn preserved

  // Reverse is never blocked by a front obstacle.
  ObstacleDecision reverse = manager.apply(forwardIntent(-0.5f, 0.0f), frontSnapshot(300));
  assert(std::fabs(reverse.intent.forward + 0.5f) < 0.0001f);
  assert(!reverse.stop_required);
}

// --- Obstacle fusion (lidar + TOF + ultrasonic -> ObstacleSnapshot) -------
void testObstacleFusion() {
  // Empty inputs -> invalid fused snapshot, all sectors 0.
  ObstacleSnapshot none = fuseObstacles(ObstacleSnapshot{}, TofSnapshot{},
                                        UltrasonicSnapshot{});
  assert(!none.valid);
  assert(none.front_center_mm == 0);
  assert(none.side_left_mm == 0);

  // Lidar sees the center far; TOF center sees nearer -> closest (TOF) wins.
  ObstacleSnapshot lidar;
  lidar.valid = true;
  lidar.last_update_ms = 100;
  lidar.front_left_mm = 0;       // no lidar reading this sector
  lidar.front_center_mm = 1500;
  lidar.front_right_mm = 900;
  lidar.side_left_mm = 700;
  lidar.side_right_mm = 0;

  TofSnapshot tof;
  tof.valid = true;
  tof.last_update_ms = 120;
  tof.front_left_valid = true;   tof.front_left_mm = 600;   // fills missing lidar sector
  tof.front_center_valid = true; tof.front_center_mm = 800;  // nearer than lidar 1500
  tof.front_right_valid = false; tof.front_right_mm = 50;    // invalid -> ignored

  UltrasonicSnapshot us;
  us.valid = true;
  us.last_update_ms = 110;
  us.left_valid = true;  us.left_mm = 1200;   // lidar 700 is nearer -> lidar wins
  us.right_valid = true; us.right_mm = 400;    // fills missing lidar side

  ObstacleSnapshot f = fuseObstacles(lidar, tof, us);
  assert(f.valid);
  assert(f.last_update_ms == 120);             // newest contributor
  assert(f.front_left_mm == 600);              // TOF only
  assert(f.front_center_mm == 800);            // TOF nearer than lidar
  assert(f.front_right_mm == 900);             // invalid TOF ignored, lidar kept
  assert(f.side_left_mm == 700);               // lidar nearer than ultrasonic
  assert(f.side_right_mm == 400);              // ultrasonic only

  // A dead lidar but live TOF still yields a valid forward picture.
  ObstacleSnapshot tof_only = fuseObstacles(ObstacleSnapshot{}, tof, UltrasonicSnapshot{});
  assert(tof_only.valid);
  assert(tof_only.front_center_mm == 800);
  assert(tof_only.front_right_mm == 0);        // invalid TOF + no lidar -> no reading
}

// --- Sensor task ingestion (UART -> parser -> App state) -------------------
void testSensorIngestion() {
  // Mirror the firmware path: raw UWB bytes -> parser -> snapshot.
  UwbGcP2304Parser parser;
  parser.reset();
  feedUwb(parser, {0xF0, 0x06, 0x03, 0x00, 0x73, 0x00, 0x14, 0x00, 0xBC, 0xAA},
          1000, true);

  ObstacleSnapshot obstacle;
  obstacle.valid = true;
  obstacle.last_update_ms = 1000;
  obstacle.front_center_mm = 1500;

  PowerStatus power;
  power.valid = true;
  power.last_update_ms = 1000;
  power.battery_voltage = 37.0f;
  power.low_battery = false;

  App app;
  app.begin();
  app.ingestSensorInputs(parser.target(), obstacle, power, ImuSnapshot{},
                         TofSnapshot{}, SensorDiagnostics{},
                         UltrasonicSnapshot{}, CameraStatus{},
                         false, 1000, 1000);
  app.tick(1000);

  // Parsed UWB + obstacle + power + heartbeats land in SystemState verbatim.
  assert(app.state().uwb.valid);
  assert(app.state().uwb.distance_mm == 1150);
  assert(app.state().obstacle.valid);
  assert(app.state().obstacle.front_center_mm == 1500);
  assert(app.state().power.valid);
  assert(std::fabs(app.state().power.battery_voltage - 37.0f) < 0.0001f);
  assert(app.state().heartbeat.uwb_task_ms == 1000);
  assert(app.state().heartbeat.sensor_task_ms == 1000);

  // RC ingestion populates state.rc for the MANUAL_RC path.
  RcInput rc;
  rc.online = true;
  rc.last_update_ms = 1000;
  rc.throttle = 0.4f;
  rc.steering = -0.2f;
  app.ingestRcInput(rc);
  assert(app.state().rc.online);
  assert(std::fabs(app.state().rc.throttle - 0.4f) < 0.0001f);
  assert(std::fabs(app.state().rc.steering + 0.2f) < 0.0001f);
}

// --- H5 command handler (panel events -> H5ControlInput) ------------------
void testH5CommandHandler() {
  H5CommandHandler h5;
  h5.reset();

  // No connection -> jog ignored, nothing moves.
  assert(!h5.onJog(1, 0.5f, 0.0f, true, 10));
  assert(!h5.input().connected);

  // Connect: present but not yet authorised to move.
  h5.onConnect(100);
  assert(h5.input().connected);
  assert(!h5.input().unlock_request);
  assert(std::fabs(h5.input().throttle) < 0.0001f);

  // Deadman-held jog -> motion authorised, speed clamped to -1..1.
  assert(h5.onJog(1, 1.5f, -0.3f, true, 120));
  assert(h5.input().unlock_request);
  assert(std::fabs(h5.input().throttle - 1.0f) < 0.0001f);  // clamped
  assert(std::fabs(h5.input().steering + 0.3f) < 0.0001f);

  // Replayed / out-of-order seq is rejected.
  assert(!h5.onJog(1, 0.2f, 0.2f, true, 130));
  assert(std::fabs(h5.input().throttle - 1.0f) < 0.0001f);

  // deadman released -> immediate stop.
  assert(h5.onJog(2, 0.8f, 0.0f, false, 140));
  assert(!h5.input().unlock_request);
  assert(std::fabs(h5.input().throttle) < 0.0001f);

  // AUTO_FOLLOW is only a request; mode flag set, motion stays stopped.
  h5.onModeRequest(H5ModeRequest::AUTO_FOLLOW_REQUEST, 150);
  assert(h5.input().auto_request);
  assert(!h5.input().unlock_request);

  // AUTO request is a valid mode confirmation; mode_manager applies the safety
  // gates (wizard + calibration + UWB) without requiring a jog unlock.
  ModeManager modes;
  SystemState auto_state;
  auto_state.mode = RunMode::SAFE_IDLE;
  auto_state.estop_active = false;
  auto_state.h5 = h5.input();
  auto_state.install_wizard_complete = true;
  auto_state.throttle_calibrated = true;
  auto_state.uwb.valid = true;
  assert(modes.selectMode(auto_state, SafetyDecision{}) == RunMode::AUTO_FOLLOW);

  auto_state.mode = RunMode::AUTO_FOLLOW;
  auto_state.h5.safe_idle_request = true;
  assert(modes.selectMode(auto_state, SafetyDecision{}) == RunMode::SAFE_IDLE);

  SystemState cloud_state;
  cloud_state.mode = RunMode::SAFE_IDLE;
  cloud_state.estop_active = false;
  cloud_state.cloud.connected = true;
  cloud_state.cloud.unlock_request = true;
  assert(modes.selectMode(cloud_state, SafetyDecision{}) ==
         RunMode::MANUAL_CLOUD_LOW_SPEED);

  // Staleness (> H5_LOST_STOP_MS since last command) zeroes motion.
  h5.onJog(3, 0.6f, 0.0f, true, 160);
  assert(!h5.input().auto_request);
  assert(h5.input().unlock_request);
  h5.update(160 + 1001);
  assert(!h5.input().unlock_request);
  assert(std::fabs(h5.input().throttle) < 0.0001f);

  // Disconnect clears everything.
  h5.onDisconnect();
  assert(!h5.input().connected);

  // App ingestion plumbs the snapshot into SystemState.
  App app;
  app.begin();
  H5ControlInput snap;
  snap.connected = true;
  snap.unlock_request = true;
  snap.throttle = 0.05f;
  app.ingestH5Input(snap);
  assert(app.state().h5.connected);
  assert(app.state().h5.unlock_request);
  assert(std::fabs(app.state().h5.throttle - 0.05f) < 0.0001f);
}

// --- H5 transport: state JSON serialisation -------------------------------
bool jsonContains(const char* json, const char* needle) {
  return std::strstr(json, needle) != nullptr;
}

void testTelemetryJson() {
  SystemState state;
  state.now_ms = 123456;
  state.mode = RunMode::SAFE_IDLE;
  state.safety.stop_reason = StopReason::NONE;
  state.uwb.valid = true;
  state.uwb.last_update_ms = 123000;
  state.uwb.distance_mm = 1500;
  state.obstacle.valid = true;
  state.obstacle.last_update_ms = 123100;
  state.obstacle.front_center_mm = 800;
  state.tof.valid = true;
  state.tof.last_update_ms = 123200;
  state.tof.front_center_valid = true;
  state.tof.front_center_mm = 790;
  state.sensor_diagnostics.lidar_valid = true;
  state.sensor_diagnostics.lidar_rx_bytes = 1200;
  state.sensor_diagnostics.lidar_packets = 20;
  state.sensor_diagnostics.lidar_scans = 2;
  state.sensor_diagnostics.lidar_front_center_mm = 810;
  state.sensor_diagnostics.tof_init_ok_mask = 7;
  state.sensor_diagnostics.tof_init_attempt_count = 4;
  state.sensor_diagnostics.tof_init_failure_count = 1;
  state.sensor_diagnostics.tof_read_count = 30;
  state.ultrasonic.valid = true;
  state.ultrasonic.last_update_ms = 123300;
  state.ultrasonic.left_valid = true;
  state.ultrasonic.left_mm = 900;
  state.power.battery_voltage = 37.5f;
  state.motor_command.brake = true;
  state.install_wizard_complete = true;
  state.throttle_calibrated = true;
  state.cloud.connected = true;
  state.cloud.last_update_ms = 123400;
  state.cloud.last_seq = 42;
  state.imu.valid = true;
  state.imu.last_update_ms = 123450;
  state.imu.yaw_deg = 45.0f;
  state.imu.yaw_rate_dps = 3.5f;
  state.imu.pitch_deg = -1.2f;
  state.imu.roll_deg = 0.8f;

  char buf[2560];
  const size_t n = buildStateJson(state, buf, sizeof(buf));
  assert(n > 0);
  assert(buf[n] == '\0');
  assert(jsonContains(buf, "\"mode\":\"SAFE_IDLE\""));
  assert(jsonContains(buf, "\"stop_reason\":\"NONE\""));
  assert(jsonContains(buf, "\"cloud\":{\"connected\":true"));
  assert(jsonContains(buf, "\"last_seq\":42"));
  assert(jsonContains(buf, "\"imu\":{\"valid\":true"));
  assert(jsonContains(buf, "\"yaw_deg\":45.0"));
  assert(jsonContains(buf, "\"yaw_rate_dps\":3.5"));
  assert(jsonContains(buf, "\"distance_mm\":1500"));
  assert(jsonContains(buf, "\"last_update_ms\":123000"));
  assert(jsonContains(buf, "\"front_center_mm\":800"));
  assert(jsonContains(buf, "\"tof\":{"));
  assert(jsonContains(buf, "\"front_center_valid\":true"));
  assert(jsonContains(buf, "\"lidar\":{\"valid\":true"));
  assert(jsonContains(buf, "\"rx_bytes\":1200"));
  assert(jsonContains(buf, "\"init_ok_mask\":7"));
  assert(jsonContains(buf, "\"init_attempt_count\":4"));
  assert(jsonContains(buf, "\"init_failure_count\":1"));
  assert(jsonContains(buf, "\"ultrasonic\":{"));
  assert(jsonContains(buf, "\"left_valid\":true"));
  assert(jsonContains(buf, "\"camera\":{\"online\":false,\"stream_url\":\"http://192.168.4.10/stream\"}"));
  assert(jsonContains(buf, "\"battery_voltage\":37.50"));
  assert(jsonContains(buf, "\"brake\":true"));
  assert(jsonContains(buf, "\"install_wizard_complete\":true"));
  assert(jsonContains(buf, "\"throttle_calibrated\":true"));

  // Too-small buffer never emits a malformed fragment.
  char tiny[16];
  assert(buildStateJson(state, tiny, sizeof(tiny)) == 0);
  assert(tiny[0] == '\0');

  // Enum mappings.
  assert(std::strcmp(modeToString(RunMode::AUTO_FOLLOW), "AUTO_FOLLOW") == 0);
  assert(std::strcmp(modeToString(RunMode::MANUAL_CLOUD_LOW_SPEED),
                     "MANUAL_CLOUD_LOW_SPEED") == 0);
  assert(std::strcmp(stopReasonToString(StopReason::UWB_LOST), "UWB_LOST") == 0);
  assert(std::strcmp(stopReasonToString(StopReason::CLOUD_LOST),
                     "CLOUD_LOST") == 0);
}

// --- H5 transport: request body parsing -----------------------------------
void testRequestParser() {
  // Valid jog with deadman held.
  const char* jog = "{\"seq\":7,\"forward\":0.5,\"turn\":-0.25,\"deadman\":true}";
  JogRequest a = parseJogRequest(jog, std::strlen(jog));
  assert(a.valid);
  assert(a.seq == 7);
  assert(std::fabs(a.forward - 0.5f) < 0.0001f);
  assert(std::fabs(a.turn + 0.25f) < 0.0001f);
  assert(a.deadman);

  // deadman absent -> fail-safe false, still a valid (stop) request.
  const char* jog2 = "{\"seq\":8,\"forward\":0.0,\"turn\":0.0}";
  JogRequest b = parseJogRequest(jog2, std::strlen(jog2));
  assert(b.valid);
  assert(!b.deadman);

  // Missing mandatory field -> invalid.
  const char* bad = "{\"forward\":0.5,\"turn\":0.0,\"deadman\":true}";
  assert(!parseJogRequest(bad, std::strlen(bad)).valid);
  assert(!parseJogRequest(nullptr, 0).valid);

  // Mode requests.
  const char* m1 = "{\"requested_mode\":\"MANUAL_H5_LOW_SPEED\"}";
  assert(parseModeRequest(m1, std::strlen(m1)) == H5ModeRequest::MANUAL_H5_LOW_SPEED);
  const char* m2 = "{\"requested_mode\":\"AUTO_FOLLOW_REQUEST\"}";
  assert(parseModeRequest(m2, std::strlen(m2)) == H5ModeRequest::AUTO_FOLLOW_REQUEST);
  const char* m3 = "{\"requested_mode\":\"MANUAL_RC\"}";  // not H5-settable
  assert(parseModeRequest(m3, std::strlen(m3)) == H5ModeRequest::NONE);

  // End-to-end: parsed jog drives the handler.
  H5CommandHandler h5;
  h5.onConnect(100);
  assert(h5.onJog(a.seq, a.forward, a.turn, a.deadman, 110));
  assert(h5.input().unlock_request);
  assert(std::fabs(h5.input().throttle - 0.5f) < 0.0001f);

  const char* wizard =
      "{\"complete\":true,\"estop_checked\":true,\"wheels_lifted\":true,"
      "\"direction_checked\":true,\"throttle_checked\":true}";
  WizardRequest wr = parseWizardRequest(wizard, std::strlen(wizard));
  assert(wr.valid);
  assert(wr.complete);
  const char* incomplete_wizard =
      "{\"complete\":true,\"estop_checked\":true,\"wheels_lifted\":true,"
      "\"direction_checked\":true,\"throttle_checked\":false}";
  assert(!parseWizardRequest(incomplete_wizard, std::strlen(incomplete_wizard)).valid);
}

}  // namespace

int main() {
  testTimeUtils();
  testSafetyAndMixer();
  testUwbParser();
  testFollowController();
  testLidarParser();
  testImuParser();
  testFollowPipeline();
  testObstacleManager();
  testObstacleFusion();
  testSensorIngestion();
  testH5CommandHandler();
  testTelemetryJson();
  testRequestParser();
  return 0;
}
