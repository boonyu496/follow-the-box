// Shared constants and pure helpers for the FollowBox cloud console.
(function initFollowBoxCloudShared(global) {
  const modeLabels = {
    BOOT_SELF_TEST: "启动自检",
    SAFE_IDLE: "安全停",
    MANUAL_RC: "遥控手动",
    MANUAL_H5_LOW_SPEED: "H5 手动",
    MANUAL_CLOUD_LOW_SPEED: "云端低速",
    AUTO_FOLLOW: "自动跟随",
    FAULT_LOCKOUT: "故障锁定",
    ESTOP_ACTIVE: "急停触发",
  };

  const stopLabels = {
    NONE: "无停车原因",
    ESTOP: "急停",
    RC_LOST: "遥控丢失",
    H5_LOST: "H5 断线",
    CLOUD_LOST: "云端断线",
    UWB_LOST: "UWB 丢失",
    OBSTACLE_STOP: "障碍停车",
    LOW_BATTERY: "低电压",
    SENSOR_TIMEOUT: "传感器超时",
    MOTOR_FAULT: "电机故障",
    INSTALL_WIZARD_NOT_DONE: "安装向导未完成",
    WATCHDOG_TIMEOUT: "看门狗超时",
  };

  const staStatusLabels = {
    0: "IDLE",
    1: "NO_SSID",
    2: "SCAN_DONE",
    3: "CONNECTED",
    4: "CONNECT_FAILED",
    5: "CONNECTION_LOST",
    6: "DISCONNECTED",
  };

  const DEFAULT_OPERATOR_TOKEN = "";
  const MAP_MAX_MM = 4000;
  const DEVICE_ONLINE_TTL_MS = 10000;
  const TOF_RATE_WINDOW_MS = 5000;

  // Browser storage is local convenience only; never bake operator tokens into H5.
  const STORAGE_KEY_TOKEN = "fb_operator_token_v2";
  const STORAGE_KEY_DEVICE = "fb_device_id";
  const STORAGE_KEY_CAMERA = "fb_camera_url";
  const STORAGE_KEY_CAMERA_LAST = "fb_camera_last_url";

  function loadSetting(storageKey, defaultValue) {
    try {
      const stored = localStorage.getItem(storageKey);
      if (stored !== null && stored !== "") return stored;
    } catch (e) { /* localStorage disabled */ }
    try {
      const legacy = sessionStorage.getItem(storageKey);
      if (legacy !== null && legacy !== "") {
        saveSetting(storageKey, legacy);
        return legacy;
      }
    } catch (e) { /* sessionStorage disabled */ }
    return defaultValue;
  }

  function saveSetting(storageKey, value) {
    try { localStorage.setItem(storageKey, value); }
    catch (e) { /* storage full or disabled */ }
    try { sessionStorage.setItem(storageKey, value); }
    catch (e) { /* sessionStorage disabled */ }
  }

  function wifiDiagText(wifi) {
    if (!wifi || !Object.prototype.hasOwnProperty.call(wifi, "ap_ready")) {
      return "";
    }
    const staStatus = Number(wifi.sta_status);
    const staLabel = staStatusLabels[staStatus] || String(wifi.sta_status ?? "--");
    const clients = Number(wifi.ap_clients || 0);
    const channel = wifi.wifi_channel != null ? ` ch${wifi.wifi_channel}` : "";
    return `AP ${wifi.ap_ready ? "ready" : "down"} c${clients}${channel} / STA ${staLabel}`;
  }

  function fmt(value, suffix = "", digits = 0) {
    return typeof value === "number" && Number.isFinite(value)
      ? `${value.toFixed(digits)}${suffix}` : "--";
  }

  function fmtMm(value) {
    return typeof value === "number" && value > 0 ? `${Math.round(value)}mm` : "--";
  }

  function positiveNumber(value) {
    return typeof value === "number" && Number.isFinite(value) && value > 0;
  }

  function channelValid(snapshot, validKey, valueKey) {
    if (!snapshot) return false;
    if (typeof snapshot[validKey] === "boolean") return snapshot[validKey];
    return !!snapshot.valid && positiveNumber(snapshot[valueKey]);
  }

  function channelMm(snapshot, validKey, valueKey) {
    return channelValid(snapshot, validKey, valueKey) ? `${snapshot[valueKey]}` : "--";
  }

  function validityLabel(validCount, totalCount) {
    if (validCount === totalCount) return "有效";
    if (validCount > 0) return "部分";
    return "无效";
  }

  function ageText(now, last) {
    const age = fmtAge(now, last);
    return age === "--" ? "未更新" : age;
  }

  function validCountLabel(validCount, totalCount) {
    return `${validCount}/${totalCount}`;
  }

  function bitCount3(value) {
    let v = Number(value || 0) & 0x7;
    let count = 0;
    while (v) {
      count += v & 1;
      v >>= 1;
    }
    return count;
  }

  function fmtHz(value) {
    if (!Number.isFinite(value) || value < 0) return "--";
    return `${value >= 10 ? value.toFixed(0) : value.toFixed(1)}Hz`;
  }

  function fmtLatency(value) {
    if (!Number.isFinite(value) || value < 0) return "--";
    if (value >= 1000) return `${(value / 1000).toFixed(1)}s`;
    return `${Math.round(value)}ms`;
  }

  function fmtRcPulse(value) {
    const n = Number(value);
    return Number.isFinite(n) && n > 0 ? `${Math.round(n)}us` : "--";
  }

  function fmtRcPercent(value) {
    const n = Number(value);
    return Number.isFinite(n) ? `${Math.round(n * 100)}%` : "--";
  }

  function fmtAge(now, last) {
    if (typeof now !== "number" || typeof last !== "number" || last <= 0) return "--";
    return `${Math.max(0, Math.round((now - last) / 1000))}s 前`;
  }

  function fmtWallAge(last) {
    if (typeof last !== "number" || last <= 0) return "--";
    return `${Math.max(0, Math.round((Date.now() - last) / 1000))}s 前`;
  }

  function estimateBatteryPercent(voltage) {
    if (typeof voltage !== "number" || !Number.isFinite(voltage)) return 0;
    return Math.max(0, Math.min(100, ((voltage - 30) / (42 - 30)) * 100));
  }

  function batteryVoltageSupported(voltage) {
    return typeof voltage === "number" && Number.isFinite(voltage) &&
      voltage > 0 && voltage <= 62;
  }

  function batteryDisplayText(voltage) {
    if (typeof voltage !== "number" || !Number.isFinite(voltage)) return "--";
    if (!batteryVoltageSupported(voltage)) return `${voltage.toFixed(1)}V 异常`;
    return `${voltage.toFixed(1)}V ${Math.round(estimateBatteryPercent(voltage))}%`;
  }

  global.FollowBoxCloudShared = Object.freeze({
    DEFAULT_OPERATOR_TOKEN,
    DEVICE_ONLINE_TTL_MS,
    MAP_MAX_MM,
    STORAGE_KEY_CAMERA,
    STORAGE_KEY_CAMERA_LAST,
    STORAGE_KEY_DEVICE,
    STORAGE_KEY_TOKEN,
    TOF_RATE_WINDOW_MS,
    ageText,
    batteryDisplayText,
    batteryVoltageSupported,
    bitCount3,
    channelMm,
    channelValid,
    estimateBatteryPercent,
    fmt,
    fmtAge,
    fmtHz,
    fmtLatency,
    fmtMm,
    fmtRcPercent,
    fmtRcPulse,
    fmtWallAge,
    loadSetting,
    modeLabels,
    positiveNumber,
    saveSetting,
    stopLabels,
    validCountLabel,
    validityLabel,
    wifiDiagText,
  });
})(window);
