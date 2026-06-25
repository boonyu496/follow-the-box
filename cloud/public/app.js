// FollowBox Cloud Console — Pico CSS + Notion Edition
// Transport: SSE api/device/{id}/events, POST api/device/{id}/command
// Matches firmware/web 4-tab layout for UI consistency
// Cloud-unique: spatial map, device logs, raw JSON

const $ = (id) => document.getElementById(id);

const els = {
  conn: $("conn"),
  mode: $("mode"),
  motionAllowed: $("motion-allowed"),
  motionCard: $("motion-card"),
  stopReason: $("stop-reason"),
  safetyDetail: $("safety-detail"),
  sysTime: $("sys-time"),
  battery: $("battery"),
  uwb: $("uwb"),
  uwbDetail: $("uwb-detail"),
  uwbBearing: $("uwb-bearing"),
  uwbConf: $("uwb-conf"),
  sensorSummary: $("sensor-summary"),
  sensorUwbStatus: $("sensor-uwb-status"),
  sensorUwbAge: $("sensor-uwb-age"),
  sensorImuStatus: $("sensor-imu-status"),
  sensorImuAge: $("sensor-imu-age"),
  sensorLidarStatus: $("sensor-lidar-status"),
  sensorLidarAge: $("sensor-lidar-age"),
  sensorTofStatus: $("sensor-tof-status"),
  sensorTofAge: $("sensor-tof-age"),
  sensorUltraStatus: $("sensor-ultra-status"),
  sensorUltraAge: $("sensor-ultra-age"),
  sensorCameraStatus: $("sensor-camera-status"),
  sensorCameraAge: $("sensor-camera-age"),
  sensorPowerStatus: $("sensor-power-status"),
  sensorPowerAge: $("sensor-power-age"),
  sensorFusionStatus: $("sensor-fusion-status"),
  sensorFusionAge: $("sensor-fusion-age"),
  imuStatus: $("imu-status"),
  imuYaw: $("imu-yaw"),
  imuRate: $("imu-rate"),
  imuPr: $("imu-pr"),
  imuDetail: $("imu-detail"),
  lidarStatus: $("lidar-status"),
  lidarLeft: $("lidar-left"),
  lidarCenter: $("lidar-center"),
  lidarRight: $("lidar-right"),
  lidarDetail: $("lidar-detail"),
  tofStatus: $("tof-status"),
  tofLeft: $("tof-left"),
  tofCenter: $("tof-center"),
  tofRight: $("tof-right"),
  tofDetail: $("tof-detail"),
  tofSampleRate: $("tof-sample-rate"),
  tofChannelRate: $("tof-channel-rate"),
  tofTelemetryRate: $("tof-telemetry-rate"),
  tofDataAge: $("tof-data-age"),
  ultrasonicStatus: $("ultrasonic-status"),
  ultraLeft: $("ultra-left"),
  ultraRight: $("ultra-right"),
  ultraDetail: $("ultra-detail"),
  obstacleStatus: $("obstacle-status"),
  obstacleLeft: $("obstacle-left"),
  obstacleCenter: $("obstacle-center"),
  obstacleRight: $("obstacle-right"),
  obstacleSide: $("obstacle-side"),
  obstacleAge: $("obstacle-age"),
  sensorAuxStatus: $("sensor-aux-status"),
  sensorBatteryDetail: $("sensor-battery-detail"),
  sensorCameraDetail: $("sensor-camera-detail"),
  sensorAuxDetail: $("sensor-aux-detail"),
  motorLeft: $("motor-left"),
  motorRight: $("motor-right"),
  motorDetail: $("motor-detail"),
  driveLeft: $("drive-left"),
  driveRight: $("drive-right"),
  commandStatus: $("command-status"),
  commandAge: $("command-age"),
  lastIngest: $("last-ingest"),
  linkStatus: $("link-status"),
  rcStatus: $("rc-status"),
  rcAge: $("rc-age"),
  cloudTelemetry: $("cloud-telemetry"),
  cloudTelemetryAge: $("cloud-telemetry-age"),
  camera: $("camera"),
  cameraStream: $("camera-stream"),
  cameraPlaceholder: $("camera-placeholder"),
  cameraStatus: $("camera-status"),
  cameraUrl: $("camera-url"),
  cameraUrlState: $("camera-url-state"),
  fullscreenBtn: $("fullscreen-btn"),
  joy: $("joy"),
  stick: $("stick"),
  safeIdle: $("safe-idle"),
  deadman: $("deadman"),
  deviceId: $("device-id"),
  operatorToken: $("operator-token"),
  connectionHint: $("connection-hint"),
  saveConnection: $("save-connection"),
  saveCameraUrl: $("save-camera-url"),
  saveStatus: $("save-status"),
  logs: $("logs"),
  raw: $("raw"),
  clearLogs: $("clear-logs"),
  spatialMap: $("spatial-map"),
  otaVersion: $("ota-version"),
  otaCurrentVersion: $("ota-current-version"),
  checkOta: $("check-ota"),
  installOta: $("install-ota"),
  laterOta: $("later-ota"),
  otaStatus: $("ota-status"),
};

let checkedOtaVersion = "";

// ── Mode / Stop labels (same as firmware shared/helpers.js) ──
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

// ── Constants ──
const DEFAULT_OPERATOR_TOKEN = "0b6cf31c57bc202d002b04f843c9b430"; // matches server default
const MAX_RANGE_MM = 3000;
const MAP_MAX_MM = 4000;
const DEVICE_ONLINE_TTL_MS = 5000;
const TOF_RATE_WINDOW_MS = 5000;
// Resolve API URLs relative to app.js, so the cloud console keeps working when
// deployed under a sub-path such as https://www.boonai.cn/fb/.
const APP_BASE_URL = new URL(".", document.currentScript?.src || window.location.href);

// ── sessionStorage keys (debug phase: plaintext; will move to box-ID-derived token later) ──
const STORAGE_KEY_TOKEN = "fb_operator_token";
const STORAGE_KEY_DEVICE = "fb_device_id";
const STORAGE_KEY_CAMERA = "fb_camera_url";
const PRIVATE_CAMERA_HOSTS = new Set(["192.168.4.2", "192.168.4.10"]);

function loadSetting(storageKey, defaultValue) {
  try { return sessionStorage.getItem(storageKey) || defaultValue; }
  catch (e) { return defaultValue; }
}

function saveSetting(storageKey, value) {
  try { sessionStorage.setItem(storageKey, value); }
  catch (e) { /* storage full or disabled */ }
}

let events = null;
let sseRetryDelay = 2000;
let sseReconnectTimer = null;
let sseConnectAttempt = 0;
let manualReconnectPending = false;
let jogTimer = null;
let joyPointerId = null;
let joyForward = 0;
let joyTurn = 0;
let isFullscreen = false;
let activeCameraUrl = "";
let userCameraOverride = false;
let latestState = null;
let latestLastIngestAt = 0;
let latestTelemetryFreshAt = 0;
let sseTransportOpen = false;
let latestVideoFrameSeq = -1;
const tofRateWindow = [];

// ── Spatial Map state (RAF throttled) ──
let spatialDirty = false;
let latestSpatialData = null;

// ── Helpers ──

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

function setStatus(el, text, ok, warn = false) {
  if (!el) return;
  el.textContent = text;
  setTextState(el, ok, warn);
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

function updateTofRealtimeStats(state, tof, validCount, initMask) {
  const deviceMs = Number(state.now_ms);
  const readCount = Number(tof.read_count);
  const lastTofMs = Number(tof.last_update_ms || 0);
  if (!Number.isFinite(deviceMs) || !Number.isFinite(readCount)) {
    return { sampleHz: NaN, channelHz: NaN, telemetryHz: NaN, ageMs: NaN };
  }

  const last = tofRateWindow[tofRateWindow.length - 1];
  if (last && (deviceMs < last.deviceMs || readCount < last.readCount)) {
    tofRateWindow.length = 0;
  }
  tofRateWindow.push({ deviceMs, readCount, browserMs: performance.now() });
  while (
    tofRateWindow.length > 2 &&
    deviceMs - tofRateWindow[0].deviceMs > TOF_RATE_WINDOW_MS
  ) {
    tofRateWindow.shift();
  }

  const first = tofRateWindow[0];
  const latest = tofRateWindow[tofRateWindow.length - 1];
  const deviceSpan = latest.deviceMs - first.deviceMs;
  const browserSpan = latest.browserMs - first.browserMs;
  const sampleHz =
    deviceSpan > 0
      ? (Math.max(0, latest.readCount - first.readCount) * 1000) / deviceSpan
      : NaN;
  const telemetryHz =
    browserSpan > 0
      ? (Math.max(0, tofRateWindow.length - 1) * 1000) / browserSpan
      : NaN;
  const activeChannels = Math.max(1, bitCount3(initMask) || validCount);
  const channelHz = Number.isFinite(sampleHz) ? sampleHz / activeChannels : NaN;
  const ageMs = lastTofMs > 0 ? Math.max(0, deviceMs - lastTofMs) : NaN;
  return { sampleHz, channelHz, telemetryHz, ageMs };
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

function setTextState(el, ok, warn) {
  if (!el) return;
  el.classList.toggle("ok-text", !!ok);
  el.classList.toggle("warn-text", !ok && !!warn);
  el.classList.toggle("danger-text", !ok && !warn);
}

function deviceId() {
  return encodeURIComponent(els.deviceId.value.trim() || "followbox-001");
}

function operatorTokenValue() {
  return encodeURIComponent(els.operatorToken.value.trim());
}

function apiPath(action) {
  return new URL(`api/device/${deviceId()}/${action}`, APP_BASE_URL).toString();
}

function cloudVideoStreamUrl() {
  const token = operatorTokenValue();
  const url = new URL(apiPath("video/stream"));
  if (token) url.searchParams.set("token", decodeURIComponent(token));
  return url.toString();
}

function cloudVideoLatestUrl(frameSeq = latestVideoFrameSeq) {
  const token = operatorTokenValue();
  const url = new URL(apiPath("video/latest.jpg"));
  if (token) url.searchParams.set("token", decodeURIComponent(token));
  if (Number.isFinite(frameSeq) && frameSeq >= 0) {
    url.searchParams.set("frame", String(frameSeq));
  } else {
    url.searchParams.set("t", String(Date.now()));
  }
  return url.toString();
}

function isPrivateCameraUrl(value) {
  try {
    const url = new URL(value);
    return PRIVATE_CAMERA_HOSTS.has(url.hostname);
  } catch (e) {
    return false;
  }
}

function deviceTelemetryOnline(lastIngestAt) {
  return typeof lastIngestAt === "number" && lastIngestAt > 0 &&
    Date.now() - lastIngestAt < DEVICE_ONLINE_TTL_MS;
}

function refreshDeviceConnectionStatus() {
  if (!sseTransportOpen) return;
  const online = latestTelemetryFreshAt > 0 &&
    Date.now() - latestTelemetryFreshAt < DEVICE_ONLINE_TTL_MS;
  setOnline(
    online,
    online ? "小车遥测在线" :
      (latestLastIngestAt > 0 ? "云端已连接，小车遥测已超时" : "云端已连接，等待小车首次上报"),
  );
}

function setConnectionHint(text, state = "warn") {
  if (!els.connectionHint) return;
  els.connectionHint.textContent = text;
  els.connectionHint.classList.toggle("ok-text", state === "ok");
  els.connectionHint.classList.toggle("warn-text", state === "warn");
  els.connectionHint.classList.toggle("danger-text", state === "danger");
}

function setOnline(online, hint) {
  els.conn.textContent = online ? "已连接" : "未连接";
  els.conn.classList.toggle("fb-pill--ok", online);
  els.conn.classList.toggle("fb-pill--danger", !online);
  els.conn.classList.toggle("fb-pill--connecting", false);
  setConnectionHint(
    hint || (online ? "云端 SSE 已连接" : (navigator.onLine ? "云端 SSE 未连接" : "浏览器离线")),
    online ? "ok" : "danger",
  );
  if (online && manualReconnectPending) {
    flashStatus(els.saveStatus, "✅ 重连成功", true);
    manualReconnectPending = false;
  }
}

function setConnecting(hint = "正在连接云端 SSE...") {
  els.conn.textContent = "连接中";
  els.conn.classList.remove("fb-pill--ok", "fb-pill--danger");
  els.conn.classList.add("fb-pill--connecting");
  setConnectionHint(hint, "warn");
}

function setCameraOnline(online, text) {
  if (els.cameraPlaceholder) els.cameraPlaceholder.classList.toggle("hidden", online);
  if (els.cameraStream) els.cameraStream.classList.toggle("online", online);
  if (els.cameraStatus) els.cameraStatus.textContent = text;
}

function clearCameraStream(text) {
  activeCameraUrl = "";
  if (els.cameraStream) {
    els.cameraStream.removeAttribute("src");
    els.cameraStream.classList.remove("online");
  }
  setCameraOnline(false, text);
}

// ── View switching (matches firmware/web: data-view attribute) ──

function switchView(name) {
  document.querySelectorAll(".fb-view").forEach((view) => {
    view.classList.toggle("active", view.id === `view-${name}`);
  });
  document.querySelectorAll(".fb-nav-btn").forEach((btn) => {
    btn.classList.toggle("active", btn.dataset.view === name);
  });
  if (["drive", "sensors", "status", "settings"].includes(name) &&
      location.hash !== `#${name}`) {
    history.replaceState(null, "", `#${name}`);
  }
  // Force spatial map redraw on tab switch
  if (name === "sensors") {
    requestAnimationFrame(() => {
      setupCanvasDPI(els.spatialMap);
      if (latestSpatialData) drawSpatialMap(latestSpatialData);
    });
  }
}

document.querySelectorAll(".fb-nav-btn").forEach((btn) => {
  btn.addEventListener("click", () => {
    const tabName = btn.dataset.view;
    // ── Safety: auto-stop jog when leaving drive tab ──
    if (tabName !== "drive" && jogTimer) stopJog();
    switchView(tabName);
  });
});

// ── Camera ──

function updateCamStream(url) {
  const next = (url || "").trim();
  if (!next || next === activeCameraUrl) return;
  activeCameraUrl = next;
  els.cameraStream.src = next;
  els.cameraStatus.textContent = "加载中";
  if (els.cameraUrl) els.cameraUrl.value = next;
  if (els.cameraUrlState) els.cameraUrlState.textContent = "加载中";
}

els.cameraStream.addEventListener("load", () => setCameraOnline(true, "画面在线"));
els.cameraStream.addEventListener("error", () => setCameraOnline(false, "画面离线"));

els.cameraUrl.addEventListener("input", () => { userCameraOverride = true; });
els.cameraUrl.addEventListener("change", () => updateCamStream(els.cameraUrl.value));

// ── Fullscreen ──

function toggleFullscreen() {
  const stage = els.cameraStream?.closest(".fb-drive-stage") || els.cameraStream;
  if (!isFullscreen) {
    (stage.requestFullscreen || stage.webkitRequestFullscreen || stage.msRequestfullscreen).call(stage);
  } else {
    (document.exitFullscreen || document.webkitExitFullscreen || document.msExitFullscreen).call(document);
  }
}

function handleFullscreenChange() {
  isFullscreen = !!(document.fullscreenElement || document.webkitFullscreenElement);
  els.fullscreenBtn.textContent = isFullscreen ? "退出" : "全屏";
  setupCanvasDPI(els.spatialMap);
  setTimeout(initJoystick, 100);
}

document.addEventListener("fullscreenchange", handleFullscreenChange);
document.addEventListener("webkitfullscreenchange", handleFullscreenChange);
els.fullscreenBtn.addEventListener("click", (e) => { e.stopPropagation(); toggleFullscreen(); });

// ── Render ──

function render(payload) {
  latestLastIngestAt = Number(payload.lastIngestAt) || 0;
  const serverSaysOnline = typeof payload.online === "boolean"
    ? payload.online : deviceTelemetryOnline(latestLastIngestAt);
  latestTelemetryFreshAt = serverSaysOnline ? Date.now() : 0;
  refreshDeviceConnectionStatus();
  const s = payload.state || {};
  const safety = s.safety || {};
  const power = s.power || {};
  const uwb = s.uwb || {};
  const motor = s.motor || {};
  const lidar = s.lidar || {};
  const imu = s.imu || {};
  const tof = s.tof || {};
  const ultrasonic = s.ultrasonic || {};
  const obstacle = s.obstacle || {};
  const rc = s.rc || {};
  const cloudLink = s.cloud || {};
  const command = payload.command || {};

  latestState = s;
  if (els.otaCurrentVersion) {
    els.otaCurrentVersion.textContent = s.firmware?.version || "--";
  }
  if (payload.ota?.status && payload.ota.status !== "idle") {
    const otaLabels = {
      pending: "已授权，等待设备响应",
      restarting: "固件已校验，设备正在重启",
      installed: "更新完成，设备已确认新版本",
      failed: `更新失败：${payload.ota.reason || "未知原因"}`,
    };
    setSaveStatus(els.otaStatus, otaLabels[payload.ota.status] || payload.ota.status,
                  payload.ota.status === "failed" ? "err" : "warn");
    if (payload.ota.status === "installed") {
      els.installOta.hidden = true;
      els.laterOta.hidden = true;
    }
  }

  // Quick strip
  const mode = s.mode || "--";
  els.mode.textContent = modeLabels[mode] || mode;
  const motionAllowed = !!safety.motion_allowed;
  els.motionAllowed.textContent = motionAllowed ? "允许" : "禁止";
  setTextState(els.motionAllowed, motionAllowed);
  const stopText = stopLabels[safety.stop_reason] || safety.stop_reason || "--";
  els.stopReason.textContent = stopText;
  const batteryVoltageOk = batteryVoltageSupported(power.battery_voltage);
  const batteryPct = batteryVoltageOk ? estimateBatteryPercent(power.battery_voltage) : 0;
  els.battery.textContent = batteryDisplayText(power.battery_voltage);
  setTextState(els.battery, !power.low_battery && batteryVoltageOk,
               power.low_battery && batteryVoltageOk);

  // Sys time
  els.sysTime.textContent = s.now_ms != null ? `${Math.round(s.now_ms / 1000)}s` : "--";

  // Drive readout
  els.driveLeft.textContent = fmt(motor.left_target || 0, "", 2);
  els.driveRight.textContent = fmt(motor.right_target || 0, "", 2);

  // Status tab
  els.motionCard.textContent = motionAllowed ? "允许" : "禁止";
  setTextState(els.motionCard, motionAllowed);
  els.safetyDetail.textContent = `停车 ${stopText} / 锁定 ${safety.fault_latched ? "是" : "否"}`;

  // Command status
  if (els.commandStatus) {
    els.commandStatus.textContent = command.deadman
      ? `点动 F${Math.round((command.forward || 0) * 100)} T${Math.round((command.turn || 0) * 100)}`
      : command.safe_idle ? "安全停" : "空闲";
  }
  if (els.commandAge) {
    els.commandAge.textContent = payload.commandAt
      ? `下发 ${fmtWallAge(payload.commandAt)}` : "等待命令";
  }

  // Last ingest
  if (els.lastIngest) {
    els.lastIngest.textContent = payload.lastIngestAt
      ? new Date(payload.lastIngestAt).toLocaleTimeString() : "--";
  }

  // Link status
  if (els.linkStatus) {
    els.linkStatus.textContent = serverSaysOnline
      ? "遥测在线" : (payload.lastIngestAt ? "遥测超时" : "等待数据");
    setTextState(els.linkStatus, serverSaysOnline, !!payload.lastIngestAt);
  }

  // Physical RC and cloud-device link telemetry from firmware /ws/state.
  if (els.rcStatus) {
    els.rcStatus.textContent = rc.online ? "在线" : "离线";
    setTextState(els.rcStatus, !!rc.online, !rc.online);
  }
  if (els.rcAge) {
    els.rcAge.textContent = `更新 ${fmtAge(s.now_ms, rc.last_update_ms)}`;
  }
  if (els.cloudTelemetry) {
    els.cloudTelemetry.textContent = cloudLink.connected
      ? `seq ${cloudLink.last_seq ?? 0}` : "离线";
    setTextState(els.cloudTelemetry, !!cloudLink.connected, !cloudLink.connected);
  }
  if (els.cloudTelemetryAge) {
    els.cloudTelemetryAge.textContent = `轮询 ${fmtAge(s.now_ms, cloudLink.last_update_ms)}`;
  }

  // UWB
  els.uwb.textContent = uwb.valid ? fmt(uwb.distance_mm, "mm") : "无效";
  els.uwbBearing.textContent = uwb.valid ? `${fmt(uwb.bearing_deg, "°", 1)}` : "--";
  els.uwbConf.textContent = uwb.valid ? `q${uwb.confidence ?? 0}` : "--";
  els.uwbDetail.textContent = uwb.valid ? "目标有效" : "等待目标";
  setStatus(els.sensorUwbStatus, uwb.valid ? "有效" : "无效", !!uwb.valid, Number(uwb.last_update_ms || 0) > 0);
  if (els.sensorUwbAge) {
    els.sensorUwbAge.textContent = uwb.valid
      ? `${fmt(uwb.distance_mm, "mm")} / ${fmt(uwb.bearing_deg, "°", 1)}`
      : ageText(s.now_ms, uwb.last_update_ms);
  }

  // JY62 / WitMotion IMU
  const imuValid = !!imu.valid;
  els.imuStatus.textContent = imuValid ? "有效" : "无效";
  setTextState(els.imuStatus, imuValid, Number(imu.last_update_ms || 0) > 0);
  els.imuYaw.textContent = imuValid ? fmt(imu.yaw_deg, "°", 1) : "--";
  els.imuRate.textContent = imuValid ? fmt(imu.yaw_rate_dps, "°/s", 1) : "--";
  els.imuPr.textContent = imuValid
    ? `${fmt(imu.pitch_deg, "°", 1)} / ${fmt(imu.roll_deg, "°", 1)}`
    : "--";
  els.imuDetail.textContent = imuValid
    ? `更新 ${fmtAge(s.now_ms, imu.last_update_ms)}`
    : (Number(imu.last_update_ms || 0) > 0 ? "姿态帧已超时" : "等待 IMU 串口姿态帧");
  setStatus(els.sensorImuStatus, imuValid ? "有效" : "无效", imuValid, Number(imu.last_update_ms || 0) > 0);
  if (els.sensorImuAge) els.sensorImuAge.textContent = ageText(s.now_ms, imu.last_update_ms);

  // EAI S2 LiDAR (raw, before obstacle fusion)
  const lidarFrontCount = [
    positiveNumber(lidar.front_left_mm),
    positiveNumber(lidar.front_center_mm),
    positiveNumber(lidar.front_right_mm),
  ].filter(Boolean).length;
  els.lidarLeft.textContent = fmtMm(lidar.front_left_mm);
  els.lidarCenter.textContent = fmtMm(lidar.front_center_mm);
  els.lidarRight.textContent = fmtMm(lidar.front_right_mm);
  els.lidarStatus.textContent = lidar.valid ? validityLabel(lidarFrontCount, 3) : "无效";
  setTextState(els.lidarStatus, !!lidar.valid, Number(lidar.rx_bytes) > 0);
  setStatus(els.sensorLidarStatus, lidar.valid ? "有效" : (Number(lidar.rx_bytes) > 0 ? "有字节" : "无数据"),
            !!lidar.valid, Number(lidar.rx_bytes) > 0);
  if (els.sensorLidarAge) els.sensorLidarAge.textContent = ageText(s.now_ms, lidar.last_update_ms);
  els.lidarDetail.textContent =
    `RX ${lidar.rx_bytes || 0} / 包 ${lidar.packets || 0} / 圈 ${lidar.scans || 0}` +
    ` / 校验错 ${lidar.checksum_errors || 0} / 帧错 ${lidar.framing_errors || 0}`;

  // TOF
  const tofValidCount = [
    channelValid(tof, "front_left_valid", "front_left_mm"),
    channelValid(tof, "front_center_valid", "front_center_mm"),
    channelValid(tof, "front_right_valid", "front_right_mm"),
  ].filter(Boolean).length;
  els.tofLeft.textContent = channelMm(tof, "front_left_valid", "front_left_mm");
  els.tofCenter.textContent = channelMm(tof, "front_center_valid", "front_center_mm");
  els.tofRight.textContent = channelMm(tof, "front_right_valid", "front_right_mm");
  els.tofStatus.textContent = validityLabel(tofValidCount, 3);
  setTextState(els.tofStatus, tofValidCount === 3, tofValidCount > 0);
  setStatus(els.sensorTofStatus, validCountLabel(tofValidCount, 3),
            tofValidCount === 3, tofValidCount > 0);
  const initMask = Number(tof.init_ok_mask || 0);
  const tofRealtime = updateTofRealtimeStats(s, tof, tofValidCount, initMask);
  if (els.tofSampleRate) els.tofSampleRate.textContent = fmtHz(tofRealtime.sampleHz);
  if (els.tofChannelRate) els.tofChannelRate.textContent = fmtHz(tofRealtime.channelHz);
  if (els.tofTelemetryRate) els.tofTelemetryRate.textContent = fmtHz(tofRealtime.telemetryHz);
  if (els.tofDataAge) els.tofDataAge.textContent = fmtLatency(tofRealtime.ageMs);
  if (els.sensorTofAge) {
    els.sensorTofAge.textContent =
      `${fmtHz(tofRealtime.sampleHz)} / ${fmtLatency(tofRealtime.ageMs)}`;
  }
  els.tofDetail.textContent =
    `初始化 0b${initMask.toString(2).padStart(3, "0")}` +
    ` / 尝试 ${tof.init_attempt_count || 0} / 传感器失败 ${tof.init_failure_count || 0}` +
    ` / 读取 ${tof.read_count || 0}` +
    ` / 采集 ${fmtHz(tofRealtime.sampleHz)} / 遥测 ${fmtHz(tofRealtime.telemetryHz)}` +
    ` / NACK ${tof.mux_nack_count || 0} / 超时 ${tof.timeout_count || 0}` +
    ` / 总线恢复 ${tof.bus_clear_count || 0} / 重连 ${tof.reinit_count || 0}`;

  // Ultrasonic
  const usValidCount = [
    channelValid(ultrasonic, "left_valid", "left_mm"),
    channelValid(ultrasonic, "right_valid", "right_mm"),
  ].filter(Boolean).length;
  els.ultraLeft.textContent = channelMm(ultrasonic, "left_valid", "left_mm");
  els.ultraRight.textContent = channelMm(ultrasonic, "right_valid", "right_mm");
  els.ultrasonicStatus.textContent = validityLabel(usValidCount, 2);
  setTextState(els.ultrasonicStatus, usValidCount === 2, usValidCount > 0);
  setStatus(els.sensorUltraStatus, validCountLabel(usValidCount, 2),
            usValidCount === 2, usValidCount > 0);
  if (els.sensorUltraAge) els.sensorUltraAge.textContent = ageText(s.now_ms, ultrasonic.last_update_ms);
  if (els.ultraDetail) {
    els.ultraDetail.textContent =
      `更新 ${ageText(s.now_ms, ultrasonic.last_update_ms)} / L ${ultrasonic.left_valid ? "有效" : "无效"} / R ${ultrasonic.right_valid ? "有效" : "无效"}`;
  }

  // Obstacle
  const obstacleFrontCount = [
    positiveNumber(obstacle.front_left_mm),
    positiveNumber(obstacle.front_center_mm),
    positiveNumber(obstacle.front_right_mm),
  ].filter(Boolean).length;
  els.obstacleLeft.textContent = fmtMm(obstacle.front_left_mm);
  els.obstacleCenter.textContent = fmtMm(obstacle.front_center_mm);
  els.obstacleRight.textContent = fmtMm(obstacle.front_right_mm);
  els.obstacleStatus.textContent = obstacle.valid || obstacleFrontCount > 0
    ? validityLabel(obstacleFrontCount, 3)
    : "无效";
  setStatus(els.sensorFusionStatus, obstacle.valid ? "有效" : "无效",
            !!obstacle.valid, Number(obstacle.last_update_ms || 0) > 0);
  if (els.sensorFusionAge) els.sensorFusionAge.textContent = ageText(s.now_ms, obstacle.last_update_ms);
  if (els.obstacleSide) {
    els.obstacleSide.textContent = `${fmtMm(obstacle.side_left_mm)} / ${fmtMm(obstacle.side_right_mm)}`;
  }
  if (els.obstacleAge) els.obstacleAge.textContent = ageText(s.now_ms, obstacle.last_update_ms);

  // Motor
  els.motorLeft.textContent = fmt(motor.left_target ?? 0, "", 2);
  els.motorRight.textContent = fmt(motor.right_target ?? 0, "", 2);
  els.motorDetail.textContent = `${motor.enable ? "使能" : "关闭"}${motor.brake ? " / 刹车" : ""}`;

  // Camera
  els.camera.textContent = payload.video?.online ? "摄像头在线" : "摄像头离线";
  setStatus(els.sensorCameraStatus, payload.video?.online ? "在线" : "离线",
            !!payload.video?.online, !!payload.video?.lastFrameAt);
  if (els.sensorCameraAge) els.sensorCameraAge.textContent = payload.video?.lastFrameAt ? fmtWallAge(payload.video.lastFrameAt) : "未上报";
  if (els.sensorCameraDetail) els.sensorCameraDetail.textContent = payload.video?.online ? "在线" : "离线";
  setStatus(els.sensorPowerStatus,
            !batteryVoltageOk && power.battery_voltage != null ? "电压异常" :
              (power.low_battery ? "低电压" : (power.battery_voltage != null ? "正常" : "无数据")),
            !power.low_battery && batteryVoltageOk, power.battery_voltage != null);
  if (els.sensorPowerAge) els.sensorPowerAge.textContent = power.battery_voltage != null ? `${power.battery_voltage.toFixed(2)}V` : "未更新";
  if (els.sensorBatteryDetail) {
    els.sensorBatteryDetail.textContent = batteryVoltageOk
      ? `${power.battery_voltage.toFixed(2)}V / ${Math.round(batteryPct)}%`
      : batteryDisplayText(power.battery_voltage);
  }
  if (els.sensorAuxStatus) {
    els.sensorAuxStatus.textContent = `${power.low_battery || !batteryVoltageOk ? "电池告警" : "电池正常"} / ${payload.video?.online ? "视频在线" : "视频离线"}`;
  }
  const sensorOkCount = [
    !!uwb.valid,
    imuValid,
    !!lidar.valid,
    tofValidCount > 0,
    usValidCount > 0,
    !!payload.video?.online,
    batteryVoltageOk && !power.low_battery,
    !!obstacle.valid,
  ].filter(Boolean).length;
  if (els.sensorSummary) {
    els.sensorSummary.textContent = `在线/有效 ${sensorOkCount}/8`;
    setTextState(els.sensorSummary, sensorOkCount >= 7, sensorOkCount >= 4);
  }
  const frameSeq = Number(payload.video?.frameSeq);
  if (!userCameraOverride && payload.video?.online) {
    if (Number.isFinite(frameSeq)) latestVideoFrameSeq = frameSeq;
    updateCamStream(cloudVideoLatestUrl(latestVideoFrameSeq));
  } else if (!userCameraOverride && !payload.video?.online) {
    setCameraOnline(false, "画面离线");
  }

  // Spatial map (RAF throttled)
  latestSpatialData = payload;
  if (!spatialDirty) {
    spatialDirty = true;
    requestAnimationFrame(() => {
      drawSpatialMap(latestSpatialData);
      spatialDirty = false;
    });
  }

  // Logs
  if (payload.logs && payload.logs.length) {
    els.logs.textContent = payload.logs.slice(-120).join("\n");
  }

  // Raw JSON
  els.raw.textContent = JSON.stringify(s, null, 2);
}

// ── SSE Connection ──

function connectEvents() {
  const attempt = ++sseConnectAttempt;
  if (sseReconnectTimer) {
    clearTimeout(sseReconnectTimer);
    sseReconnectTimer = null;
  }
  if (events) events.close();
  setConnecting("正在重连云端 SSE...");
  const token = operatorTokenValue();
  const source = new EventSource(`${apiPath("events")}?token=${token}`);
  events = source;
  source.onopen = () => {
    if (attempt !== sseConnectAttempt || events !== source) return;
    sseTransportOpen = true;
    sseRetryDelay = 2000;
    setConnecting("SSE 已打开，等待首包遥测...");
  };
  source.onmessage = (ev) => {
    if (attempt !== sseConnectAttempt || events !== source) return;
    try {
      render(JSON.parse(ev.data));
    } catch (error) {
      console.warn("FollowBox telemetry parse failed", error);
      setOnline(false, "云端遥测格式错误，请检查服务器日志");
    }
  };
  source.onerror = () => {
    if (attempt !== sseConnectAttempt || events !== source) return;
    sseTransportOpen = false;
    const delay = sseRetryDelay;
    setOnline(false, `连接失败，${Math.round(delay / 1000)} 秒后自动重试（请检查 Token / 网络 / 设备上报）`);
    if (manualReconnectPending) {
      flashStatus(els.saveStatus, `❌ 重连失败，${Math.round(delay / 1000)} 秒后重试`, false);
      manualReconnectPending = false;
    }
    source.close();
    sseRetryDelay = Math.min(sseRetryDelay * 2, 30000);
    sseReconnectTimer = setTimeout(() => {
      connectEvents();
    }, delay);
  };
}

els.deviceId.addEventListener("change", connectEvents);
els.operatorToken.addEventListener("change", connectEvents);

// ── Settings: Save & Reconnect ──

function flashStatus(el, text, ok) {
  if (!el) return;
  setSaveStatus(el, text, ok ? "ok" : "err");
  if (ok) setTimeout(() => { el.textContent = ""; el.className = "fb-save-status"; }, 2500);
}

function setSaveStatus(el, text, state) {
  if (!el) return;
  el.textContent = text;
  el.className = `fb-save-status fb-save-status--${state}`;
}

els.saveConnection.addEventListener("click", () => {
  const token = els.operatorToken.value.trim();
  const devId = els.deviceId.value.trim() || "followbox-001";
  els.deviceId.value = devId;
  saveSetting(STORAGE_KEY_TOKEN, token);
  saveSetting(STORAGE_KEY_DEVICE, devId);
  manualReconnectPending = true;
  setSaveStatus(els.saveStatus, "💾 已保存，正在重连...", "warn");
  connectEvents();
});

els.saveCameraUrl.addEventListener("click", () => {
  const url = els.cameraUrl.value.trim();
  if (isPrivateCameraUrl(url)) {
    saveSetting(STORAGE_KEY_CAMERA, "");
    userCameraOverride = false;
    latestVideoFrameSeq = -1;
    clearCameraStream("等待云端画面");
    flashStatus(els.cameraUrlState, "已切回云端转发", true);
    return;
  }
  saveSetting(STORAGE_KEY_CAMERA, url);
  if (url) {
    userCameraOverride = true;
    updateCamStream(url);
  } else {
    userCameraOverride = false;
    latestVideoFrameSeq = -1;
    clearCameraStream("等待云端画面");
  }
  flashStatus(els.cameraUrlState, "✅ 视频地址已保存", true);
});

// ── OTA Version Check ──

async function checkOtaVersion() {
  if (!els.otaVersion) return;
  try {
    els.otaVersion.textContent = "检查中...";
    els.otaStatus.textContent = "";
    const token = els.operatorToken.value.trim();
    const headers = {};
    if (token) headers.Authorization = `Bearer ${token}`;
    const resp = await fetch(apiPath("firmware/version"), { headers });
    if (resp.status === 404) {
      els.otaVersion.textContent = "未发布";
      checkedOtaVersion = "";
      flashStatus(els.otaStatus, "暂无固件更新", true);
      return;
    }
    if (!resp.ok) {
      els.otaVersion.textContent = "获取失败";
      flashStatus(els.otaStatus, "❌ " + (resp.status === 401 ? "Token 无效" : "服务器错误"), false);
      return;
    }
    const data = await resp.json();
    if (data.ok) {
      els.otaCurrentVersion.textContent = data.current_version || "--";
      els.otaVersion.textContent = data.available_version || data.version || "--";
      checkedOtaVersion = data.update_available ? data.available_version : "";
      els.installOta.hidden = !checkedOtaVersion;
      els.laterOta.hidden = !checkedOtaVersion;
      if (!data.current_version) {
        setSaveStatus(els.otaStatus, "设备离线或尚未上报版本，不能安装", "warn");
      } else if (data.update_available) {
        setSaveStatus(els.otaStatus, `发现新版本 ${data.available_version}，请选择安装或暂不安装`, "warn");
      } else {
        flashStatus(els.otaStatus, "已是最新版本", true);
      }
    } else {
      els.otaVersion.textContent = "未发布";
      flashStatus(els.otaStatus, "暂无固件更新", true);
    }
  } catch (e) {
    els.otaVersion.textContent = "网络错误";
    flashStatus(els.otaStatus, "❌ 无法连接服务器", false);
  }
}

els.checkOta.addEventListener("click", checkOtaVersion);

els.installOta.addEventListener("click", async () => {
  if (!checkedOtaVersion) return;
  if (!confirm(`安装固件 ${checkedOtaVersion}？设备将强制停车、写入校验并自动重启。`)) return;
  const token = els.operatorToken.value.trim();
  const headers = { "Content-Type": "application/json" };
  if (token) headers.Authorization = `Bearer ${token}`;
  els.installOta.disabled = true;
  try {
    const resp = await fetch(apiPath("firmware/install"), {
      method: "POST",
      headers,
      body: JSON.stringify({ version: checkedOtaVersion }),
    });
    const data = await resp.json();
    if (!resp.ok || !data.ok) throw new Error(data.reason || `HTTP ${resp.status}`);
    setSaveStatus(els.otaStatus, "安装请求已提交，等待设备拉取", "warn");
    els.laterOta.hidden = true;
  } catch (e) {
    setSaveStatus(els.otaStatus, `安装请求失败：${e.message}`, "err");
    els.installOta.disabled = false;
  }
});

els.laterOta.addEventListener("click", () => {
  checkedOtaVersion = "";
  els.installOta.hidden = true;
  els.laterOta.hidden = true;
  setSaveStatus(els.otaStatus, "已暂不安装；未向设备提交任何写入请求", "ok");
});

// ── Command API ──

async function sendCommand(body) {
  const headers = { "Content-Type": "application/json" };
  const token = els.operatorToken.value.trim();
  if (token) headers.Authorization = `Bearer ${token}`;
  try {
    const resp = await fetch(apiPath("command"), {
      method: "POST",
      headers,
      body: JSON.stringify(body),
    });
    if (!resp.ok && els.commandStatus) {
      els.commandStatus.textContent = resp.status === 401 ? "命令失败：Token 无效" : `命令失败：HTTP ${resp.status}`;
      setTextState(els.commandStatus, false, false);
    }
  } catch (e) {
    if (els.commandStatus) {
      els.commandStatus.textContent = "命令失败：网络错误";
      setTextState(els.commandStatus, false, false);
    }
  }
}

// ── Jog ──

function jogBody() {
  return {
    deadman: !!jogTimer,
    forward: Math.round(joyForward * 100) / 100,
    turn: Math.round(joyTurn * 100) / 100,
  };
}

function startJog() {
  if (jogTimer) return;
  els.deadman.classList.add("active");
  sendCommand(jogBody());
  jogTimer = setInterval(() => sendCommand(jogBody()), 150);
}

function stopJog() {
  if (jogTimer) clearInterval(jogTimer);
  jogTimer = null;
  els.deadman.classList.remove("active");
  joyForward = 0;
  joyTurn = 0;
  moveStick(0, 0);
  sendCommand({ deadman: false, forward: 0, turn: 0 });
}

// Deadman button events
["mousedown", "touchstart"].forEach((e) =>
  els.deadman.addEventListener(e, (ev) => { ev.preventDefault(); startJog(); })
);
["mouseup", "mouseleave", "touchend", "touchcancel"].forEach((e) =>
  els.deadman.addEventListener(e, (ev) => { ev.preventDefault(); stopJog(); })
);

// Safe idle button
els.safeIdle.addEventListener("click", () =>
  sendCommand({ safe_idle: true, deadman: false, forward: 0, turn: 0 })
);

// ── Safety: auto-stop jog when tab hidden / screen locked ──
document.addEventListener("visibilitychange", () => {
  if (document.hidden && jogTimer) stopJog();
});
document.addEventListener("pagehide", () => { if (jogTimer) stopJog(); });

// ── Joystick ──

function moveStick(x, y) {
  if (!els.stick) return;
  els.stick.style.transform = `translate(${x}px, ${y}px)`;
}

function updateJoystick(ev) {
  const rect = els.joy.getBoundingClientRect();
  const radius = rect.width / 2;
  const max = radius * 0.62;
  let x = ev.clientX - rect.left - radius;
  let y = ev.clientY - rect.top - radius;
  const mag = Math.hypot(x, y);
  if (mag > max) {
    x = (x / mag) * max;
    y = (y / mag) * max;
  }
  moveStick(x, y);
  joyTurn = Math.max(-1, Math.min(1, x / max));
  joyForward = Math.max(-1, Math.min(1, -y / max));
}

function initJoystick() {
  if (!els.joy) return;
  els.joy.addEventListener("pointerdown", (ev) => {
    ev.preventDefault();
    joyPointerId = ev.pointerId;
    els.joy.setPointerCapture(joyPointerId);
    els.joy.classList.add("pressed");
    updateJoystick(ev);
    startJog();
  });
  els.joy.addEventListener("pointermove", (ev) => {
    if (ev.pointerId !== joyPointerId) return;
    ev.preventDefault();
    updateJoystick(ev);
  });
  const release = (ev) => {
    if (ev.pointerId !== joyPointerId) return;
    ev.preventDefault();
    els.joy.classList.remove("pressed");
    joyPointerId = null;
    stopJog();
  };
  els.joy.addEventListener("pointerup", release);
  els.joy.addEventListener("pointercancel", release);
  els.joy.addEventListener("lostpointercapture", () => {
    els.joy.classList.remove("pressed");
    joyPointerId = null;
    stopJog();
  });
}

// ── Clear Logs ──

els.clearLogs.addEventListener("click", () => { els.logs.textContent = ""; });

// ── Online/Offline detection ──

window.addEventListener("online", () => {
  connectEvents();
});
window.addEventListener("offline", () => {
  sseTransportOpen = false;
  setOnline(false);
});

setInterval(refreshDeviceConnectionStatus, 1000);

// ═══════════════════════════════════════════════
// Spatial Map (cloud-unique: top-down bird's eye)
// ═══════════════════════════════════════════════

function distColor(mm) {
  if (mm == null || mm <= 0) return "rgba(128,128,128,0.25)";
  if (mm < 500) return "#dc2626";
  if (mm < 1000) return "#dd5b00";
  return "#1aae39";
}

function distGlow(mm, rgb) {
  if (mm == null || mm <= 0) return "rgba(128,128,128,0)";
  return `rgba(${rgb},0.2)`;
}

function setupCanvasDPI(canvas) {
  if (!canvas) return;
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  const rect = canvas.getBoundingClientRect();
  if (rect.width === 0) return;
  const w = Math.round(rect.width * dpr);
  const h = Math.round(rect.height * dpr);
  if (canvas.width === w && canvas.height === h) return;
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
}

function drawSpatialMap(telemetry) {
  const canvas = els.spatialMap;
  if (!canvas) return;
  const ctx = canvas.getContext("2d");
  const W = canvas.clientWidth || canvas.width;
  const H = canvas.clientHeight || canvas.height;
  if (W === 0) return;
  const cx = W / 2, cy = H * 0.52;
  const maxPx = Math.min(cx, cy) * 0.88;
  const scale = maxPx / MAP_MAX_MM;

  ctx.clearRect(0, 0, W, H);
  ctx.fillStyle = "#0a0e18";
  ctx.fillRect(0, 0, W, H);

  // Grid & Rings
  ctx.strokeStyle = "rgba(255,255,255,0.05)";
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(cx, 0); ctx.lineTo(cx, H);
  ctx.moveTo(0, cy); ctx.lineTo(W, cy);
  ctx.stroke();

  const rings = [
    { mm: 500, label: "0.5m", alpha: 0.35 },
    { mm: 1000, label: "1m", alpha: 0.25 },
    { mm: 2000, label: "2m", alpha: 0.15 },
    { mm: 3000, label: "3m", alpha: 0.10 },
  ];
  rings.forEach((r) => {
    const px = r.mm * scale;
    ctx.strokeStyle = `rgba(255,255,255,${r.alpha})`;
    ctx.lineWidth = r.mm <= 1000 ? 1.5 : 0.5;
    ctx.setLineDash(r.mm <= 1000 ? [] : [4, 6]);
    ctx.beginPath();
    ctx.arc(cx, cy, px, 0, Math.PI * 2);
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.fillStyle = "rgba(255,255,255,0.5)";
    ctx.font = "10px system-ui, sans-serif";
    ctx.textAlign = "left";
    ctx.textBaseline = "top";
    ctx.fillText(r.label, cx + px + 4, cy - 4);
  });

  // Direction labels
  ctx.fillStyle = "rgba(255,255,255,0.35)";
  ctx.font = "11px system-ui, sans-serif";
  ctx.textAlign = "center";
  ctx.textBaseline = "middle";
  ctx.fillText("前", cx, cy - maxPx - 14);
  ctx.fillText("后", cx, cy + maxPx + 14);
  ctx.fillText("左", cx - maxPx - 14, cy);
  ctx.fillText("右", cx + maxPx + 14, cy);

  // TOF sector
  ctx.strokeStyle = "rgba(0,117,222,0.08)";
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.arc(cx, cy, maxPx, -Math.PI * 0.35, Math.PI * 0.35);
  ctx.stroke();

  // Vehicle icon
  const vSize = 20;
  ctx.fillStyle = "#d7dde8";
  ctx.beginPath();
  ctx.moveTo(cx, cy - vSize);
  ctx.lineTo(cx - vSize * 0.6, cy + vSize * 0.5);
  ctx.lineTo(cx, cy + vSize * 0.15);
  ctx.lineTo(cx + vSize * 0.6, cy + vSize * 0.5);
  ctx.closePath();
  ctx.fill();
  ctx.fillStyle = "rgba(255,255,255,0.5)";
  ctx.beginPath();
  ctx.arc(cx, cy, 3, 0, Math.PI * 2);
  ctx.fill();

  // Parse telemetry
  const s = telemetry?.state || {};
  const uwb = s.uwb || {};
  const tof = s.tof || {};
  const ultrasonic = s.ultrasonic || {};
  const obstacle = s.obstacle || {};

  function plotSensor(angleDeg, distance_mm, label, sensorType) {
    if (distance_mm == null || distance_mm <= 0) return;
    const distPx = Math.min(distance_mm, MAP_MAX_MM) * scale;
    const a = (-90 - angleDeg) * Math.PI / 180;
    const x = cx + Math.cos(a) * distPx;
    const y = cy + Math.sin(a) * distPx;
    const color = distColor(distance_mm);
    const r = sensorType === "uwb" ? 7 : 5;

    const rgb = distance_mm < 500 ? "220,38,38" : distance_mm < 1000 ? "221,91,0" : "26,174,57";
    const glow = ctx.createRadialGradient(x, y, 0, x, y, r * 3);
    glow.addColorStop(0, distGlow(distance_mm, rgb));
    glow.addColorStop(1, "rgba(0,0,0,0)");
    ctx.fillStyle = glow;
    ctx.beginPath();
    ctx.arc(x, y, r * 3, 0, Math.PI * 2);
    ctx.fill();

    ctx.fillStyle = color;
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.fill();

    if (sensorType === "uwb") {
      ctx.strokeStyle = "rgba(255,255,255,0.6)";
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc(x, y, r, 0, Math.PI * 2);
      ctx.stroke();
    }

    ctx.fillStyle = "#d7dde8";
    ctx.font = sensorType === "uwb" ? "bold 12px system-ui" : "10px system-ui";
    ctx.textAlign = "center";
    ctx.textBaseline = "bottom";
    const lx = x + (sensorType === "uwb" ? 14 : 0);
    const ly = y - r - 4;
    ctx.fillText(label, lx, ly);

    ctx.fillStyle = "rgba(255,255,255,0.5)";
    ctx.font = "9px system-ui";
    ctx.textBaseline = "top";
    if (sensorType === "uwb") {
      ctx.fillText(`${distance_mm}mm`, lx, ly + 12);
    } else {
      ctx.fillText(`${Math.round(distance_mm / 10) / 100}m`, lx, ly + 12);
    }
  }

  if (channelValid(tof, "front_left_valid", "front_left_mm") ||
      channelValid(tof, "front_center_valid", "front_center_mm") ||
      channelValid(tof, "front_right_valid", "front_right_mm")) {
    plotSensor(-35, tof.front_left_mm, "TOF左", "tof");
    plotSensor(0, tof.front_center_mm, "TOF中", "tof");
    plotSensor(35, tof.front_right_mm, "TOF右", "tof");
  }

  if (obstacle.valid || positiveNumber(obstacle.front_left_mm) ||
      positiveNumber(obstacle.front_center_mm) ||
      positiveNumber(obstacle.front_right_mm)) {
    plotSensor(-30, obstacle.front_left_mm, "障左", "obstacle");
    plotSensor(0, obstacle.front_center_mm, "障前", "obstacle");
    plotSensor(30, obstacle.front_right_mm, "障右", "obstacle");
  }

  if (channelValid(ultrasonic, "left_valid", "left_mm") ||
      channelValid(ultrasonic, "right_valid", "right_mm")) {
    plotSensor(-90, ultrasonic.left_mm, "超声左", "ultra");
    plotSensor(90, ultrasonic.right_mm, "超声右", "ultra");
  }

  if (uwb.valid && uwb.distance_mm > 0) {
    const bearing = Math.max(-85, Math.min(85, uwb.bearing_deg || 0));
    plotSensor(bearing, uwb.distance_mm, "目标", "uwb");
  }

  ctx.fillStyle = "rgba(255,255,255,0.2)";
  ctx.font = "9px system-ui";
  ctx.textAlign = "right";
  ctx.textBaseline = "bottom";
  ctx.fillText(`范围 ${MAP_MAX_MM / 1000}m`, W - 8, H - 8);
}

// ── Init ──

// Restore settings from sessionStorage; fall back to defaults
const savedToken = loadSetting(STORAGE_KEY_TOKEN, DEFAULT_OPERATOR_TOKEN);
const savedDevice = loadSetting(STORAGE_KEY_DEVICE, "followbox-001");
const savedCameraRaw = loadSetting(STORAGE_KEY_CAMERA, "");
const savedCamera = isPrivateCameraUrl(savedCameraRaw) ? "" : savedCameraRaw;
if (savedCameraRaw && !savedCamera) saveSetting(STORAGE_KEY_CAMERA, "");

if (els.operatorToken) els.operatorToken.value = savedToken;
if (els.deviceId) els.deviceId.value = savedDevice;
if (els.cameraUrl && savedCamera) els.cameraUrl.value = savedCamera;

setupCanvasDPI(els.spatialMap);
drawSpatialMap({});
initJoystick();
const initialView = (location.hash || "").slice(1);
if (["drive", "sensors", "status", "settings"].includes(initialView)) {
  switchView(initialView);
}
connectEvents();
if (savedCamera) {
  userCameraOverride = true;
  updateCamStream(savedCamera);
} else {
  setCameraOnline(false, "等待云端画面");
}
