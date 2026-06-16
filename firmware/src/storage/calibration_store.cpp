#include "storage/calibration_store.h"

#include <Preferences.h>

#include <algorithm>

#include "config/profile_defaults.h"
#include "telemetry/debug_console.h"

namespace followbox {
namespace {

constexpr char kNamespace[] = "fb_calib";
constexpr char kKeySchema[] = "schema";
constexpr char kKeyDeadband[] = "deadband";
constexpr char kKeyMinActive[] = "min_active";
constexpr char kKeyMax[] = "max";
constexpr char kKeyFullScale[] = "full_scale";
constexpr char kKeyRise[] = "rise";
constexpr char kKeyFall[] = "fall";
constexpr uint16_t kSchemaVersion = 1;

// Hard physical ceiling for a 0-5V analog throttle module. Anything above this
// is rejected as corrupt regardless of what NVS holds.
constexpr int kModuleHardMaxMv = 5000;
constexpr int kSlewMinPerS = 1;
constexpr int kSlewMaxPerS = 100000;

int clampInt(int v, int lo, int hi) {
  return std::max(lo, std::min(hi, v));
}

}  // namespace

ThrottleCalibration CalibrationStore::sanitize(const ThrottleCalibration& cal) {
  ThrottleCalibration out = cal;

  // Full-scale is the reference for every voltage field; clamp it first.
  out.module_full_scale_mv = clampInt(out.module_full_scale_mv, 1000, kModuleHardMaxMv);

  // deadband <= min_active <= max <= full_scale, all >= 0.
  out.deadband_mv = clampInt(out.deadband_mv, 0, out.module_full_scale_mv);
  out.min_active_mv =
      clampInt(out.min_active_mv, out.deadband_mv, out.module_full_scale_mv);
  out.max_mv =
      clampInt(out.max_mv, out.min_active_mv + 1, out.module_full_scale_mv);

  // Slew rates must be strictly positive so a stored 0 can never freeze the
  // ramp (which would make the throttle ignore commanded changes).
  out.rise_mv_per_s = clampInt(out.rise_mv_per_s, kSlewMinPerS, kSlewMaxPerS);
  out.fall_mv_per_s = clampInt(out.fall_mv_per_s, kSlewMinPerS, kSlewMaxPerS);
  return out;
}

bool CalibrationStore::equal(const ThrottleCalibration& a,
                             const ThrottleCalibration& b) {
  return a.deadband_mv == b.deadband_mv && a.min_active_mv == b.min_active_mv &&
         a.max_mv == b.max_mv && a.module_full_scale_mv == b.module_full_scale_mv &&
         a.rise_mv_per_s == b.rise_mv_per_s && a.fall_mv_per_s == b.fall_mv_per_s;
}

bool CalibrationStore::begin() {
  Preferences prefs;
  opened_ = prefs.begin(kNamespace, /*readOnly=*/false);
  if (opened_) {
    prefs.end();
  } else {
    FB_LOGW("calibration_store: NVS open failed, using defaults");
  }
  return opened_;
}

ThrottleCalibration CalibrationStore::load() {
  ThrottleCalibration cal;  // compile-time safe defaults
  calibrated_ = false;

  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/true)) {
    cached_ = cal;
    cache_valid_ = true;
    return cal;
  }

  const uint16_t schema = prefs.getUShort(kKeySchema, 0);
  if (schema == kSchemaVersion) {
    cal.deadband_mv = prefs.getInt(kKeyDeadband, cal.deadband_mv);
    cal.min_active_mv = prefs.getInt(kKeyMinActive, cal.min_active_mv);
    cal.max_mv = prefs.getInt(kKeyMax, cal.max_mv);
    cal.module_full_scale_mv = prefs.getInt(kKeyFullScale, cal.module_full_scale_mv);
    cal.rise_mv_per_s = prefs.getInt(kKeyRise, cal.rise_mv_per_s);
    cal.fall_mv_per_s = prefs.getInt(kKeyFall, cal.fall_mv_per_s);
    calibrated_ = true;
  } else if (schema != 0) {
    FB_LOGW("calibration_store: schema %u != %u, using defaults", schema,
            kSchemaVersion);
  }
  prefs.end();

  // Always clamp before handing values to the drive layer.
  cal = sanitize(cal);
  cached_ = cal;
  cache_valid_ = true;
  FB_LOGI("calibration_store: loaded calibrated=%d dead=%d min=%d max=%d fs=%d",
          calibrated_ ? 1 : 0, cal.deadband_mv, cal.min_active_mv, cal.max_mv,
          cal.module_full_scale_mv);
  return cal;
}

bool CalibrationStore::save(const ThrottleCalibration& cal) {
  const ThrottleCalibration clean = sanitize(cal);

  if (calibrated_ && cache_valid_ && equal(cached_, clean)) {
    return true;  // nothing changed, skip the Flash write
  }

  Preferences prefs;
  if (!prefs.begin(kNamespace, /*readOnly=*/false)) {
    FB_LOGE("calibration_store: save open failed");
    return false;
  }
  prefs.putUShort(kKeySchema, kSchemaVersion);
  prefs.putInt(kKeyDeadband, clean.deadband_mv);
  prefs.putInt(kKeyMinActive, clean.min_active_mv);
  prefs.putInt(kKeyMax, clean.max_mv);
  prefs.putInt(kKeyFullScale, clean.module_full_scale_mv);
  prefs.putInt(kKeyRise, clean.rise_mv_per_s);
  prefs.putInt(kKeyFall, clean.fall_mv_per_s);
  prefs.end();

  cached_ = clean;
  cache_valid_ = true;
  calibrated_ = true;
  FB_LOGI("calibration_store: saved dead=%d min=%d max=%d fs=%d", clean.deadband_mv,
          clean.min_active_mv, clean.max_mv, clean.module_full_scale_mv);
  return true;
}

}  // namespace followbox
