// FollowBox H5 control panel — Pico CSS + Notion Edition
// Transport: WebSocket /ws/state for telemetry, POST /api/* for control
// The panel only requests low-speed jog and modes;
// firmware safety still gates every motor command.

const $ = (id) => document.getElementById(id);

const els = {
  conn: $("conn"),
  mode: $("mode"),
  motionAllowed: $("motion-allowed"),
  motionCard: $("motion-card"),
  stopReason: $("stop-reason"),
  safetyDetail: $("safety-detail"),
  speedScale: $("speed-scale"),
  sysTime: $("sys-time"),
  battery: $("battery"),
  uwbMini: $("uwb-mini"),
  uwb: $("uwb"),
  uwbDetail: $("uwb-detail"),
  uwbBearing: $("uwb-bearing"),
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
  obstacle: $("obstacle"),
  obstacleFrontDetail: $("obstacle-front-detail"),
  obstacleSideDetail: $("obstacle-side-detail"),
  obstacleAge: $("obstacle-age"),
  imu: $("imu"),
  imuYaw: $("imu-yaw"),
  imuRate: $("imu-rate"),
  imuPr: $("imu-pr"),
  imuDetail: $("imu-detail"),
  lidar: $("lidar"),
  lidarLeft: $("lidar-left"),
  lidarCenter: $("lidar-center"),
  lidarRight: $("lidar-right"),
  lidarDetail: $("lidar-detail"),
  motor: $("motor"),
  motorDetail: $("motor-detail"),
  driveLeft: $("drive-left"),
  driveRight: $("drive-right"),
  rc: $("rc"),
  rcAge: $("rc-age"),
  cloud: $("cloud"),
  cloudAge: $("cloud-age"),
  tof: $("tof"),
  tofLeft: $("tof-left"),
  tofCenter: $("tof-center"),
  tofRight: $("tof-right"),
  tofDetail: $("tof-detail"),
  tofSampleRate: $("tof-sample-rate"),
  tofChannelRate: $("tof-channel-rate"),
  tofTelemetryRate: $("tof-telemetry-rate"),
  tofDataAge: $("tof-data-age"),
  tofLeftBar: $("tof-left-bar"),
  tofCenterBar: $("tof-center-bar"),
  tofRightBar: $("tof-right-bar"),
  ultrasonic: $("ultrasonic"),
  ultraLeft: $("ultra-left"),
  ultraRight: $("ultra-right"),
  ultraDetail: $("ultra-detail"),
  sensorAuxStatus: $("sensor-aux-status"),
  sensorBatteryDetail: $("sensor-battery-detail"),
  sensorCameraDetail: $("sensor-camera-detail"),
  sensorAuxDetail: $("sensor-aux-detail"),
  camera: $("camera"),
  cameraLink: $("camera-link"),
  cameraStream: $("camera-stream"),
  cameraPlaceholder: $("camera-placeholder"),
  cameraStatus: $("camera-status"),
  cameraUrl: $("camera-url"),
  cameraUrlState: $("camera-url-state"),
  fullscreenBtn: $("fullscreen-btn"),
  btnCameraReload: $("btn-camera-reload"),
  logs: $("logs"),
  copyLogs: $("copy-logs"),
  clearLogs: $("clear-logs"),
  joy: $("joy"),
  stick: $("stick"),
  spatialMap: $("spatial-map"),
  uwbCanvas: $("uwb-canvas"),
  obstacleCanvas: $("obstacle-canvas"),
};

const extEls = {
  resetFault: $("reset-fault"),
  btnSaveCal: $("btn-save-cal"),
  btnWizardComplete: $("btn-wizard-complete"),
  calDeadband: $("cal-deadband"),
  calMinActive: $("cal-min-active"),
  calMax: $("cal-max"),
  calFullScale: $("cal-full-scale"),
  calRise: $("cal-rise"),
  calFall: $("cal-fall"),
  wizEstop: $("wiz-estop"),
  wizWheels: $("wiz-wheels"),
  wizDirection: $("wiz-direction"),
  wizThrottle: $("wiz-throttle"),
  localApiKey: $("local-api-key"),
  localKeyStatus: $("local-key-status"),
  btnLocalKeySave: $("btn-local-key-save"),
};

const wifiEls = {
  status: $("wifi-sta-status"),
  ip: $("wifi-sta-ip"),
  ssid: $("wifi-ssid"),
  pass: $("wifi-pass"),
  save: $("btn-wifi-save"),
};

const otaEls = {
  state: $("ota-local-state"),
  current: $("ota-current-version"),
  available: $("ota-available-version"),
  check: $("btn-ota-check"),
  install: $("btn-ota-install"),
  later: $("btn-ota-later"),
  hint: $("ota-local-hint"),
  directState: $("ota-direct-state"),
  directFile: $("ota-direct-file"),
  directUpload: $("btn-ota-direct-upload"),
  directProgress: $("ota-direct-progress"),
  directHint: $("ota-direct-hint"),
};

// modeLabels / stopLabels → loaded from ../shared/helpers.js

const LOCAL_KEY_STORAGE = "followbox.localApiKey"; // sessionStorage — cleared on tab close
const CAMERA_URL_STORAGE = "followbox.cameraStreamUrl";
const CLOUD_VIDEO_BASE_URL = "https://www.boonai.cn/fb";
const CLOUD_VIDEO_DEVICE_ID = "followbox-001";
// Empty by default: do not bake cloud operator credentials into device H5.
const CLOUD_VIDEO_OPERATOR_TOKEN = "";
const PRIVATE_CAMERA_HOSTS = new Set(["192.168.4.2", "192.168.4.10"]);
const HTTP_DIAGNOSTIC_TIMEOUT_MS = 2200;
const BROWSER_LOG_LIMIT = 80;
const MAX_RANGE_MM = 3000;
const MAP_MAX_MM = 4000;
const TOF_RATE_WINDOW_MS = 5000;

let ws = null;
let jogSeq = 1;
let jogTimer = null;
let joyPointerId = null;
let joyForward = 0;
let joyTurn = 0;
let isFullscreen = false;
let fullscreenOrientationLocked = false;
let activeCameraUrl = "";
let lastTelemetryCameraUrl = "";
let userCameraOverride = false;
let cameraImageOnline = false;
let cloudVideoTimer = null;
let cameraRetryTimer = null;
let cameraRetryDelay = 3000;
let localAuthStatus = null;
let latestState = null;
let lastStateAt = 0;
let logsApiUnavailableLogged = false;
let stateFallbackUnavailableLogged = false;
let lastLoggedMode = "";
let lastLoggedCameraOnline = null;
let lastLoggedCameraUrl = "";
const deviceLogs = [];
const browserLogs = [];
const tofRateWindow = [];
const spatialTrail = [];
let lastSpatialTrailKey = "";

// ── Canvas redraw state (RAF throttled) ──
let canvasDirty = false;
let latestUwbData = null;
let latestObstacleData = null;

// ── Helpers ──

function localApiKey() {
  return sessionStorage.getItem(LOCAL_KEY_STORAGE) || "";
}

function authHeaders() {
  const headers = { "Content-Type": "application/json" };
  const key = localApiKey();
  if (key) headers["X-FollowBox-Key"] = key;
  return headers;
}

function refreshLocalKeyUi() {
  if (!extEls.localApiKey || !extEls.localKeyStatus) return;
  const key = localApiKey();
  extEls.localApiKey.value = key;
  if (localAuthStatus && !localAuthStatus.auth_required) {
    extEls.localKeyStatus.textContent = "未启用";
    extEls.localApiKey.placeholder = "当前固件不需要 X-FollowBox-Key";
    setTextState(extEls.localKeyStatus, true, false);
    return;
  }
  if (localAuthStatus?.auth_required && !localAuthStatus?.key_configured) {
    extEls.localKeyStatus.textContent = "固件未配置 Key";
    setTextState(extEls.localKeyStatus, false, true);
    return;
  }
  extEls.localKeyStatus.textContent = key ? "已设置" : "需输入";
  extEls.localApiKey.placeholder = "X-FollowBox-Key";
  setTextState(extEls.localKeyStatus, !!key, !key);
}

async function refreshLocalAuthStatus() {
  if (!extEls.localKeyStatus) return;
  try {
    const res = await fetch("/api/local-auth/status", { cache: "no-store" });
    if (!res.ok) return;
    localAuthStatus = await res.json();
    refreshLocalKeyUi();
  } catch (e) {
    /* older firmware: keep the manual key field usable */
  }
}

// fmt / fmtMm / fmtAge / estimateBatteryPercent / setTextState → loaded from ../shared/helpers.js

function positiveNumber(value) {
  return typeof value === "number" && Number.isFinite(value) && value > 0;
}

function channelValid(snapshot, validKey, valueKey) {
  if (!snapshot) return false;
  if (typeof snapshot[validKey] === "boolean") return snapshot[validKey];
  return !!snapshot.valid && positiveNumber(snapshot[valueKey]);
}

function channelMm(snapshot, validKey, valueKey) {
  return channelValid(snapshot, validKey, valueKey) ? fmtMm(snapshot[valueKey]) : "--";
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

function sensorOnline(snapshot, now, staleMs = 1000) {
  if (!snapshot) return false;
  if (snapshot.valid) return true;
  const last = Number(snapshot.last_update_ms || 0);
  return last > 0 && typeof now === "number" && now - last <= staleMs;
}

function validCountLabel(validCount, totalCount) {
  return `${validCount}/${totalCount}`;
}

function sensorDistanceColor(mm) {
  if (!positiveNumber(mm)) return "rgba(128,138,148,.3)";
  if (mm < 500) return "#dc2626";
  if (mm < 1000) return "#dd5b00";
  return "#1aae39";
}

function polarToCanvas(cx, cy, scale, bearingDeg, distanceMm) {
  const distance = Math.min(Math.max(0, distanceMm || 0), MAP_MAX_MM);
  const angle = (bearingDeg - 90) * Math.PI / 180;
  return {
    x: cx + Math.cos(angle) * distance * scale,
    y: cy + Math.sin(angle) * distance * scale,
  };
}

function updateSpatialTrail(state) {
  const uwb = state?.uwb || {};
  if (!uwb.valid || !positiveNumber(uwb.distance_mm)) return;
  const bearing = Math.max(-120, Math.min(120, Number(uwb.bearing_deg || 0)));
  const distance = Math.min(MAP_MAX_MM, Number(uwb.distance_mm));
  const key = `${Math.round(bearing * 2) / 2}:${Math.round(distance / 40)}`;
  if (key === lastSpatialTrailKey) return;
  lastSpatialTrailKey = key;
  spatialTrail.push({ bearing, distance, at: performance.now() });
  while (spatialTrail.length > 28) spatialTrail.shift();
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

function setConn(online) {
  els.conn.textContent = online ? "已连接" : "未连接";
  els.conn.classList.toggle("fb-pill--ok", online);
  els.conn.classList.toggle("fb-pill--danger", !online);
}

function logTimestamp() {
  return new Date().toTimeString().slice(0, 8);
}

function renderLogs() {
  if (!els.logs) return;
  const merged = deviceLogs.concat(browserLogs).slice(-120);
  if (merged.length) {
    els.logs.textContent = merged.join("\n");
  } else {
    els.logs.textContent = "-- 等待日志 --";
  }
}

function appendBrowserLog(message, level = "I") {
  browserLogs.push(`[${logTimestamp()}][${level}] ${message}`);
  while (browserLogs.length > BROWSER_LOG_LIMIT) browserLogs.shift();
  renderLogs();
}

async function fetchJsonWithTimeout(path, timeoutMs = HTTP_DIAGNOSTIC_TIMEOUT_MS) {
  if (typeof AbortController === "undefined") {
    const res = await fetch(path, { cache: "no-store" });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return res.json();
  }
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), timeoutMs);
  try {
    const res = await fetch(path, { cache: "no-store", signal: controller.signal });
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    return await res.json();
  } finally {
    clearTimeout(timer);
  }
}

function isApPage() {
  return location.hostname === "192.168.4.1" || location.hostname === "";
}

function isPrivateCameraUrl(value) {
  try {
    const url = new URL(value);
    return PRIVATE_CAMERA_HOSTS.has(url.hostname);
  } catch (e) {
    return false;
  }
}

function shouldUseCloudVideoRelay(value) {
  return CLOUD_VIDEO_OPERATOR_TOKEN.length > 0 && !isApPage() && isPrivateCameraUrl(value);
}

function cloudVideoStreamUrl() {
  const url = new URL(
    `api/device/${encodeURIComponent(CLOUD_VIDEO_DEVICE_ID)}/video/stream`,
    CLOUD_VIDEO_BASE_URL.endsWith("/") ? CLOUD_VIDEO_BASE_URL : `${CLOUD_VIDEO_BASE_URL}/`,
  );
  if (CLOUD_VIDEO_OPERATOR_TOKEN) {
    url.searchParams.set("token", CLOUD_VIDEO_OPERATOR_TOKEN);
  }
  return url.toString();
}

function stopCloudVideoRelay() {
  if (cloudVideoTimer) {
    clearInterval(cloudVideoTimer);
    cloudVideoTimer = null;
  }
}

function startCloudVideoRelay(sourceUrl) {
  if (!els.cameraStream || !els.cameraUrl) return;
  const relayKey = `cloud-relay:${sourceUrl}`;
  els.cameraUrl.value = sourceUrl;
  if (els.cameraUrlState) els.cameraUrlState.textContent = "局域网使用云端低帧率转发";
  if (activeCameraUrl !== relayKey) {
    activeCameraUrl = relayKey;
    els.cameraStatus.textContent = "加载云端画面";
    setCameraVisible(true, "加载云端画面");
    els.cameraStream.src = cloudVideoStreamUrl();
    appendBrowserLog(`LAN 无法直连 ${sourceUrl} 时改用云端视频流`);
  }
}

function retryCloudVideoRelay() {
  if (cloudVideoTimer || !activeCameraUrl.startsWith("cloud-relay:")) return;
  cloudVideoTimer = setTimeout(() => {
    cloudVideoTimer = null;
    if (!activeCameraUrl.startsWith("cloud-relay:") || !els.cameraStream) return;
    appendBrowserLog("云端视频流断开，重试连接", "W");
    setCameraVisible(true, "重试云端画面");
    els.cameraStream.src = cloudVideoStreamUrl();
  }, 5000);
}

function setCameraStream(url) {
  if (!els.cameraStream || !els.cameraUrl) return;
  const next = (url || els.cameraUrl.value || "").trim();
  if (!next || next === activeCameraUrl) return;
  if (shouldUseCloudVideoRelay(next)) {
    startCloudVideoRelay(next);
    return;
  }
  stopCloudVideoRelay();
  activeCameraUrl = next;
  els.cameraUrl.value = next;
  if (els.cameraUrlState) els.cameraUrlState.textContent = next;
  els.cameraStatus.textContent = "加载中";
  setCameraVisible(true, "加载中");
  els.cameraStream.src = next;
  appendBrowserLog(`加载视频 ${next}`);
}

function saveCameraOverride(url) {
  const next = (url || "").trim();
  if (next) {
    localStorage.setItem(CAMERA_URL_STORAGE, next);
    userCameraOverride = true;
    setCameraStream(next);
    if (els.cameraUrlState) els.cameraUrlState.textContent = "已保存";
  } else {
    localStorage.removeItem(CAMERA_URL_STORAGE);
    userCameraOverride = false;
    if (lastTelemetryCameraUrl) setCameraStream(lastTelemetryCameraUrl);
  }
}

function restoreCameraOverride() {
  if (!els.cameraUrl) return;
  const saved = localStorage.getItem(CAMERA_URL_STORAGE) || "";
  if (saved) {
    userCameraOverride = true;
    els.cameraUrl.value = saved;
    setCameraStream(saved);
  }
}

function setCameraVisible(visible, text) {
  if (els.cameraPlaceholder) els.cameraPlaceholder.classList.toggle("hidden", visible);
  if (els.cameraStream) els.cameraStream.classList.toggle("online", visible);
  if (els.cameraStatus && text) els.cameraStatus.textContent = text;
}

function setCameraOnline(online, text) {
  cameraImageOnline = !!online;
  setCameraVisible(online, text);
}

function scheduleCameraRetry() {
  if (activeCameraUrl.startsWith("cloud-relay:")) {
    retryCloudVideoRelay();
    return;
  }
  if (cameraRetryTimer || !activeCameraUrl) {
    return;
  }
  const retryUrl = activeCameraUrl;
  const delay = cameraRetryDelay;
  cameraRetryDelay = Math.min(Math.round(cameraRetryDelay * 1.5), 15000);
  cameraRetryTimer = setTimeout(() => {
    cameraRetryTimer = null;
    if (activeCameraUrl === retryUrl) {
      activeCameraUrl = "";
      setCameraStream(retryUrl);
    }
  }, delay);
}

function activeFullscreenElement() {
  return document.fullscreenElement || document.webkitFullscreenElement || null;
}

function fullscreenRequestFor(el) {
  return el?.requestFullscreen || el?.webkitRequestFullscreen || el?.msRequestFullscreen;
}

function fullscreenExitFor(doc) {
  return doc.exitFullscreen || doc.webkitExitFullscreen || doc.msExitFullscreen;
}

async function lockLandscapeForFullscreen() {
  const orientation = screen?.orientation;
  if (!orientation?.lock) return;
  try {
    await orientation.lock("landscape");
    fullscreenOrientationLocked = true;
  } catch (e) {
    // Some mobile browsers only allow orientation lock after trusted gestures.
  }
}

function unlockFullscreenOrientation() {
  if (!fullscreenOrientationLocked) return;
  try {
    screen?.orientation?.unlock?.();
  } catch (e) {
    // Ignore unsupported unlock paths.
  }
  fullscreenOrientationLocked = false;
}

async function toggleFullscreen() {
  const stage = els.cameraStream?.closest(".fb-drive-stage") || els.cameraStream;
  if (!stage) return;
  if (!isFullscreen) {
    const request = fullscreenRequestFor(stage);
    if (!request) return;
    try {
      await request.call(stage);
      await lockLandscapeForFullscreen();
    } catch (e) {
      document.body.classList.remove("fb-force-landscape");
    }
  } else {
    const exit = fullscreenExitFor(document);
    if (exit) {
      try {
        await exit.call(document);
      } catch (e) {
        // Fullscreen state is reconciled by handleFullscreenChange.
      }
    }
  }
}

function handleFullscreenChange() {
  isFullscreen = !!activeFullscreenElement();
  document.body.classList.toggle("fb-force-landscape", isFullscreen);
  if (!isFullscreen) unlockFullscreenOrientation();
  if (els.fullscreenBtn) els.fullscreenBtn.textContent = isFullscreen ? "退出" : "全屏";
  setupCanvasDPI(els.spatialMap);
  setupCanvasDPI(els.uwbCanvas);
  setupCanvasDPI(els.obstacleCanvas);
}

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
  requestAnimationFrame(() => {
    if (latestState) {
      setupCanvasDPI(els.spatialMap);
      setupCanvasDPI(els.uwbCanvas);
      setupCanvasDPI(els.obstacleCanvas);
      drawSpatialMap(latestState, performance.now());
      drawUwb(latestState.uwb || {});
      drawObstacle(latestState.obstacle || {});
    }
  });
}

// ── Render ──

function renderState(s) {
  latestState = s;
  lastStateAt = Date.now();
  updateSpatialTrail(s);
  const mode = s.mode ?? "--";
  if (mode !== lastLoggedMode) {
    appendBrowserLog(`模式 ${lastLoggedMode || "--"} -> ${mode}`);
    lastLoggedMode = mode;
  }
  els.mode.textContent = modeLabels[mode] ?? mode;
  els.sysTime.textContent = s.now_ms != null ? `${Math.round(s.now_ms / 1000)}s` : "--";

  const sf = s.safety ?? {};
  const motionAllowed = !!sf.motion_allowed;
  els.motionAllowed.textContent = motionAllowed ? "允许" : "禁止";
  els.motionCard.textContent = motionAllowed ? "允许" : "禁止";
  setTextState(els.motionAllowed, motionAllowed);
  setTextState(els.motionCard, motionAllowed);
  const stopText = stopLabels[sf.stop_reason] ?? sf.stop_reason ?? "--";
  els.stopReason.textContent = stopText;
  els.safetyDetail.textContent = `停车 ${stopText} / 锁定 ${sf.fault_latched ? "是" : "否"}`;
  els.speedScale.textContent =
    sf.max_speed_scale != null ? `限速 ${Math.round(sf.max_speed_scale * 100)}%` : "限速 --";

  const p = s.power ?? {};
  const batteryVoltageOk = batteryVoltageSupported(p.battery_voltage);
  const batteryPct = batteryVoltageOk ? estimateBatteryPercent(p.battery_voltage) : 0;
  els.battery.textContent = batteryDisplayText(p.battery_voltage);
  setTextState(els.battery, !p.low_battery && batteryVoltageOk,
               p.low_battery && batteryVoltageOk);

  const u = s.uwb ?? {};
  els.uwb.textContent = u.valid ? fmt(u.distance_mm, "mm") : "无效";
  els.uwbMini.textContent = u.valid ? fmt(u.distance_mm / 1000, "m", 1) : "--";
  els.uwbBearing.textContent = u.valid ? `${fmt(u.bearing_deg, "°", 1)} / q${u.confidence ?? 0}` : "--";
  els.uwbDetail.textContent = u.valid ? "目标有效" : "等待目标";
  setStatus(els.sensorUwbStatus, u.valid ? "有效" : "无效", !!u.valid, Number(u.last_update_ms || 0) > 0);
  if (els.sensorUwbAge) {
    els.sensorUwbAge.textContent = u.valid
      ? `${fmt(u.distance_mm, "mm")} / ${fmt(u.bearing_deg, "°", 1)}`
      : ageText(s.now_ms, u.last_update_ms);
  }
  latestUwbData = u;

  // Raw EAI S2 data, before TOF/ultrasonic obstacle fusion. Keeping this
  // separate makes a dead or misconfigured lidar visible instead of allowing
  // another ranging sensor to make the fused obstacle card look healthy.
  const lidar = s.lidar ?? {};
  const lidarRxBytes = Number(lidar.rx_bytes || 0);
  const lidarPackets = Number(lidar.packets || 0);
  const lidarScans = Number(lidar.scans || 0);
  const lidarChecksumErrors = Number(lidar.checksum_errors || 0);
  const lidarFramingErrors = Number(lidar.framing_errors || 0);
  els.lidarLeft.textContent = fmtMm(lidar.front_left_mm);
  els.lidarCenter.textContent = fmtMm(lidar.front_center_mm);
  els.lidarRight.textContent = fmtMm(lidar.front_right_mm);

  let lidarDiagnosis = "等待串口数据";
  if (lidar.valid) {
    lidarDiagnosis = "扫描有效";
  } else if (lidarRxBytes === 0) {
    lidarDiagnosis = "无串口数据：检查供电、DATA/TX→GPIO3、CTL/RX←GPIO43 和共地";
  } else if (lidarPackets === 0) {
    lidarDiagnosis = "有字节但无有效包：检查型号、波特率和协议";
  } else if (lidarScans === 0) {
    lidarDiagnosis = "已解包但未完成一圈：检查电机转动和起始包";
  } else {
    lidarDiagnosis = "扫描数据已超时";
  }
  els.lidar.textContent = lidar.valid ? "有效" : "无效";
  setTextState(els.lidar, !!lidar.valid, lidarRxBytes > 0);
  setStatus(els.sensorLidarStatus, lidar.valid ? "有效" : (lidarRxBytes > 0 ? "有字节" : "无数据"),
            !!lidar.valid, lidarRxBytes > 0);
  if (els.sensorLidarAge) els.sensorLidarAge.textContent = ageText(s.now_ms, lidar.last_update_ms);
  els.lidarDetail.textContent =
    `侧向 ${fmtMm(lidar.side_left_mm)} / ${fmtMm(lidar.side_right_mm)}` +
    ` · RX ${lidarRxBytes} · 包 ${lidarPackets} · 圈 ${lidarScans}` +
    ` · 校验错 ${lidarChecksumErrors} · 帧错 ${lidarFramingErrors} · ${lidarDiagnosis}`;

  const o = s.obstacle ?? {};
  const hasFrontObstacle = positiveNumber(o.front_left_mm) ||
    positiveNumber(o.front_center_mm) || positiveNumber(o.front_right_mm);
  const hasSideObstacle = positiveNumber(o.side_left_mm) || positiveNumber(o.side_right_mm);
  els.obstacle.textContent = hasFrontObstacle || !hasSideObstacle
    ? `${fmtMm(o.front_left_mm)} / ${fmtMm(o.front_center_mm)} / ${fmtMm(o.front_right_mm)}`
    : `侧 ${fmtMm(o.side_left_mm)} / ${fmtMm(o.side_right_mm)}`;
  setStatus(els.sensorFusionStatus, o.valid ? "有效" : "无效", !!o.valid, Number(o.last_update_ms || 0) > 0);
  if (els.sensorFusionAge) els.sensorFusionAge.textContent = ageText(s.now_ms, o.last_update_ms);
  if (els.obstacleFrontDetail) {
    els.obstacleFrontDetail.textContent =
      `${fmtMm(o.front_left_mm)} / ${fmtMm(o.front_center_mm)} / ${fmtMm(o.front_right_mm)}`;
  }
  if (els.obstacleSideDetail) {
    els.obstacleSideDetail.textContent = `${fmtMm(o.side_left_mm)} / ${fmtMm(o.side_right_mm)}`;
  }
  if (els.obstacleAge) els.obstacleAge.textContent = ageText(s.now_ms, o.last_update_ms);
  latestObstacleData = o;

  const imu = s.imu ?? {};
  const imuValid = !!imu.valid;
  els.imu.textContent = imuValid ? "有效" : "无效";
  setTextState(els.imu, imuValid, Number(imu.last_update_ms || 0) > 0);
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

  // ── Canvas redraw (RAF-throttled — WS may push at 5-10 Hz) ──
  if (!canvasDirty) {
    canvasDirty = true;
    requestAnimationFrame(() => {
      drawUwb(latestUwbData);
      drawObstacle(latestObstacleData);
      canvasDirty = false;
    });
  }

  const tof = s.tof ?? {};
  const tofValidCount = [
    channelValid(tof, "front_left_valid", "front_left_mm"),
    channelValid(tof, "front_center_valid", "front_center_mm"),
    channelValid(tof, "front_right_valid", "front_right_mm"),
  ].filter(Boolean).length;
  els.tof.textContent = validityLabel(tofValidCount, 3);
  setTextState(els.tof, tofValidCount === 3, tofValidCount > 0);
  setStatus(els.sensorTofStatus, validCountLabel(tofValidCount, 3),
            tofValidCount === 3, tofValidCount > 0);
  setBar("left", tof.front_left_mm, channelValid(tof, "front_left_valid", "front_left_mm"));
  setBar("center", tof.front_center_mm, channelValid(tof, "front_center_valid", "front_center_mm"));
  setBar("right", tof.front_right_mm, channelValid(tof, "front_right_valid", "front_right_mm"));
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
  const initAttempts = Number(tof.init_attempt_count || 0);
  const initFailures = Number(tof.init_failure_count || 0);
  const muxNacks = Number(tof.mux_nack_count || 0);
  let tofDiagnosis = "等待初始化诊断";
  const tofValidValues = [
    channelValid(tof, "front_left_valid", "front_left_mm") ? Number(tof.front_left_mm) : 0,
    channelValid(tof, "front_center_valid", "front_center_mm") ? Number(tof.front_center_mm) : 0,
    channelValid(tof, "front_right_valid", "front_right_mm") ? Number(tof.front_right_mm) : 0,
  ].filter((value) => value > 0);
  const suspiciousNearTof = tofValidValues.length > 0 &&
    tofValidValues.every((value) => value < 300);
  if (suspiciousNearTof) {
    tofDiagnosis = "读数持续小于30cm：优先检查探头是否看到车壳/地面、保护膜、安装孔遮挡或朝向过低";
  } else if (tofValidCount > 0) {
    tofDiagnosis = `读取正常，累计 ${tof.read_count || 0}`;
  } else if (muxNacks > 0) {
    tofDiagnosis = "TCA9548A 无响应：检查 3.3V、GPIO10/11、上拉和地址";
  } else if (initFailures > 0) {
    tofDiagnosis = "MUX 已响应，VL53L1X 无响应：检查通道供电和接线";
  } else if (initAttempts > 0) {
    tofDiagnosis = "TOF 尚未产生有效距离";
  }
  els.tofDetail.textContent =
    `初始化 0b${initMask.toString(2).padStart(3, "0")} / 尝试 ${initAttempts}` +
    ` / 失败 ${initFailures} / 读取 ${tof.read_count || 0}` +
    ` / 超时 ${tof.timeout_count || 0} / NACK ${muxNacks}` +
    ` / 采集 ${fmtHz(tofRealtime.sampleHz)} / 遥测 ${fmtHz(tofRealtime.telemetryHz)}` +
    ` / BusClear ${tof.bus_clear_count || 0} / 重连 ${tof.reinit_count || 0}` +
    ` / ${tofDiagnosis}`;

  const us = s.ultrasonic ?? {};
  const usValidCount = [
    channelValid(us, "left_valid", "left_mm"),
    channelValid(us, "right_valid", "right_mm"),
  ].filter(Boolean).length;
  els.ultrasonic.textContent = validityLabel(usValidCount, 2);
  setTextState(els.ultrasonic, usValidCount === 2, usValidCount > 0);
  setStatus(els.sensorUltraStatus, validCountLabel(usValidCount, 2),
            usValidCount === 2, usValidCount > 0);
  if (els.sensorUltraAge) els.sensorUltraAge.textContent = ageText(s.now_ms, us.last_update_ms);
  els.ultraLeft.textContent = channelMm(us, "left_valid", "left_mm");
  els.ultraRight.textContent = channelMm(us, "right_valid", "right_mm");
  if (els.ultraDetail) {
    els.ultraDetail.textContent =
      `更新 ${ageText(s.now_ms, us.last_update_ms)} / L ${us.left_valid ? "有效" : "无效"} / R ${us.right_valid ? "有效" : "无效"}`;
  }

  const m = s.motor ?? {};
  els.motor.textContent = m.enable ? "使能" : "关闭";
  els.motorDetail.textContent =
    `L ${fmt(m.left_target ?? 0, "", 2)} / R ${fmt(m.right_target ?? 0, "", 2)}${m.brake ? " / 刹车" : ""}`;
  els.driveLeft.textContent = fmt(m.left_target ?? 0, "", 2);
  els.driveRight.textContent = fmt(m.right_target ?? 0, "", 2);

  const rc = s.rc ?? {};
  els.rc.textContent = rc.online ? "在线" : "离线";
  const rcChannels = Array.isArray(rc.ch_us) ? rc.ch_us : [];
  const rcPulseText =
    `CH1-5 ${[0, 1, 2, 3, 4].map((i) => fmtRcPulse(rcChannels[i])).join("/")}`;
  els.rcAge.textContent = rc.online
    ? `${rcPulseText} · 转向 ${fmtRcPercent(rc.steering)} · 油门 ${fmtRcPercent(rc.throttle)} · 限速 ${fmtRcPercent(rc.speed_limit)} · STOP ${rc.stop_switch ? "ON" : "OFF"} · AUTO ${rc.auto_request ? "REQ" : "off"}`
    : `${rcPulseText} · 更新 ${fmtAge(s.now_ms, rc.last_update_ms)}`;
  setTextState(els.rc, !!rc.online, !rc.online);

  const cloud = s.cloud ?? {};
  els.cloud.textContent = cloud.connected ? `seq ${cloud.last_seq ?? 0}` : "离线";
  els.cloudAge.textContent = fmtAge(s.now_ms, cloud.last_update_ms);
  setTextState(els.cloud, !!cloud.connected, !cloud.connected);

  const cam = s.camera ?? {};
  const cameraVisible = !!cam.online || cameraImageOnline;
  if (cameraVisible !== lastLoggedCameraOnline) {
    appendBrowserLog(`视频状态 ${cameraVisible ? "在线" : "离线"}`, cameraVisible ? "I" : "W");
    lastLoggedCameraOnline = cameraVisible;
  }
  const cameraRelayText = activeCameraUrl.startsWith("cloud-relay:")
    ? "云端转发"
    : (cam.stream_url ? cam.stream_url : "无地址");
  els.camera.textContent = cameraVisible ? "摄像头在线" : "摄像头离线";
  els.cameraLink.textContent = cameraVisible ? "在线" : "离线";
  setTextState(els.cameraLink, cameraVisible, !cameraVisible);
  setStatus(els.sensorCameraStatus, cameraVisible ? "在线" : "离线", cameraVisible, !cameraVisible);
  if (els.sensorCameraAge) els.sensorCameraAge.textContent = cam.stream_url ? "有地址" : "无地址";
  if (els.sensorCameraDetail) {
    els.sensorCameraDetail.textContent =
      `${cameraVisible ? "在线" : "离线"} / ${cameraRelayText}`;
  }
  setStatus(els.sensorPowerStatus,
            !batteryVoltageOk && p.battery_voltage != null ? "电压异常" :
              (p.low_battery ? "低电压" : (p.battery_voltage != null ? "正常" : "无数据")),
            !p.low_battery && batteryVoltageOk, p.battery_voltage != null);
  if (els.sensorPowerAge) els.sensorPowerAge.textContent = p.battery_voltage != null ? `${p.battery_voltage.toFixed(2)}V` : "未更新";
  if (els.sensorBatteryDetail) {
    els.sensorBatteryDetail.textContent = batteryVoltageOk
      ? `${p.battery_voltage.toFixed(2)}V / ${Math.round(batteryPct)}%`
      : batteryDisplayText(p.battery_voltage);
  }
  if (els.sensorAuxStatus) {
    els.sensorAuxStatus.textContent = `${p.low_battery || !batteryVoltageOk ? "电池告警" : "电池正常"} / ${cameraVisible ? "视频在线" : "视频离线"}`;
  }
  if (cam.stream_url) {
    lastTelemetryCameraUrl = cam.stream_url;
    if (cam.stream_url !== lastLoggedCameraUrl) {
      appendBrowserLog(`遥测视频地址 ${cam.stream_url}`);
      lastLoggedCameraUrl = cam.stream_url;
    }
    if (!userCameraOverride) setCameraStream(cam.stream_url);
  }

  const sensorOkCount = [
    !!u.valid,
    imuValid,
    !!lidar.valid,
    tofValidCount > 0,
    usValidCount > 0,
    cameraVisible,
    batteryVoltageOk && !p.low_battery,
    !!o.valid,
  ].filter(Boolean).length;
  if (els.sensorSummary) {
    els.sensorSummary.textContent = `在线/有效 ${sensorOkCount}/8`;
    setTextState(els.sensorSummary, sensorOkCount >= 7, sensorOkCount >= 4);
  }

  const wiz = $("wizard-status");
  if (wiz) {
    wiz.textContent = s.install_wizard_complete ? "已完成" : "未完成";
    setTextState(wiz, !!s.install_wizard_complete, !s.install_wizard_complete);
  }
  const throttleCal = $("throttle-cal-status");
  if (throttleCal) {
    throttleCal.textContent = s.throttle_calibrated ? "已完成" : "未完成";
    setTextState(throttleCal, !!s.throttle_calibrated, !s.throttle_calibrated);
  }

  document.querySelectorAll(".fb-mode-btn").forEach((btn) => {
    const target = btn.dataset.mode;
    btn.classList.toggle(
      "active",
      target === mode || (target === "AUTO_FOLLOW_REQUEST" && mode === "AUTO_FOLLOW")
    );
  });
}

// ── TOF Bars ──

function setBar(name, value, valid = positiveNumber(value)) {
  const label = els[`tof${name[0].toUpperCase()}${name.slice(1)}`];
  const bar = els[`tof${name[0].toUpperCase()}${name.slice(1)}Bar`];
  if (label) label.textContent = valid ? fmtMm(value) : "--";
  if (!bar) return;
  const pct = valid
    ? Math.max(8, Math.min(100, (value / MAX_RANGE_MM) * 100))
    : 0;
  bar.style.height = `${pct}%`;
  bar.classList.toggle("danger", valid && value < 500);
  bar.classList.toggle("warn", valid && value >= 500 && value < 1000);
}

// ── Canvas DPI setup (Retina display sharpness) ──
function setupCanvasDPI(canvas) {
  if (!canvas) return;
  const dpr = Math.min(window.devicePixelRatio || 1, 2);
  const rect = canvas.getBoundingClientRect();
  if (rect.width === 0) return;
  const w = Math.round(rect.width * dpr);
  const h = Math.round(rect.height * dpr);
  if (canvas.width === w && canvas.height === h) return; // dirty check
  canvas.width = w;
  canvas.height = h;
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0); // avoids scale accumulation
}

// ── Canvas: Full spatial sensor map ──

function drawSpatialMap(state, now = performance.now()) {
  const canvas = els.spatialMap;
  if (!canvas) return;
  setupCanvasDPI(canvas);
  const ctx = canvas.getContext("2d");
  const W = canvas.clientWidth || canvas.width;
  const H = canvas.clientHeight || canvas.height;
  if (!W || !H) return;

  const cx = W / 2;
  const cy = H / 2;
  const maxPx = Math.min(W, H) * 0.39;
  const scale = maxPx / MAP_MAX_MM;
  const sweep = ((now / 3200) % 1) * Math.PI * 2 - Math.PI / 2;
  const pulse = 0.5 + 0.5 * Math.sin(now / 360);

  ctx.clearRect(0, 0, W, H);
  ctx.fillStyle = "#060a12";
  ctx.fillRect(0, 0, W, H);

  const bg = ctx.createRadialGradient(cx, cy, 0, cx, cy, maxPx * 1.35);
  bg.addColorStop(0, "rgba(0,213,255,.10)");
  bg.addColorStop(0.55, "rgba(0,117,222,.04)");
  bg.addColorStop(1, "rgba(0,0,0,0)");
  ctx.fillStyle = bg;
  ctx.beginPath();
  ctx.arc(cx, cy, maxPx * 1.35, 0, Math.PI * 2);
  ctx.fill();

  [500, 1000, 2000, 3000, 4000].forEach((mm) => {
    const r = mm * scale;
    ctx.strokeStyle = mm <= 1000 ? "rgba(0,213,255,.24)" : "rgba(255,255,255,.08)";
    ctx.lineWidth = mm <= 1000 ? 1.2 : 1;
    ctx.setLineDash(mm > 1000 ? [4, 6] : []);
    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, Math.PI * 2);
    ctx.stroke();
    ctx.setLineDash([]);
    ctx.fillStyle = "rgba(255,255,255,.44)";
    ctx.font = "10px system-ui";
    ctx.textAlign = "left";
    ctx.textBaseline = "middle";
    ctx.fillText(`${mm / 1000}m`, cx + r + 5, cy);
  });

  [-90, -60, -30, 0, 30, 60, 90].forEach((deg) => {
    const end = polarToCanvas(cx, cy, scale, deg, MAP_MAX_MM);
    ctx.strokeStyle = deg === 0 ? "rgba(0,213,255,.24)" : "rgba(255,255,255,.07)";
    ctx.lineWidth = deg === 0 ? 1.5 : 1;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(end.x, end.y);
    ctx.stroke();
    if (deg !== 0) {
      const label = polarToCanvas(cx, cy, scale, deg, MAP_MAX_MM + 180);
      ctx.fillStyle = "rgba(255,255,255,.34)";
      ctx.font = "10px system-ui";
      ctx.textAlign = "center";
      ctx.textBaseline = "middle";
      ctx.fillText(`${deg}°`, label.x, label.y);
    }
  });

  ctx.strokeStyle = `rgba(0,213,255,${0.24 + pulse * 0.16})`;
  ctx.lineWidth = 2;
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  ctx.lineTo(cx + Math.cos(sweep) * maxPx, cy + Math.sin(sweep) * maxPx);
  ctx.stroke();

  const cam = state?.camera || {};
  if (cam.online || cam.stream_url) {
    ctx.fillStyle = cam.online ? "rgba(0,213,255,.10)" : "rgba(128,138,148,.07)";
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    const left = polarToCanvas(cx, cy, scale, -24, 2400);
    const right = polarToCanvas(cx, cy, scale, 24, 2400);
    ctx.lineTo(left.x, left.y);
    ctx.arc(cx, cy, 2400 * scale, (-114 * Math.PI) / 180, (-66 * Math.PI) / 180);
    ctx.lineTo(right.x, right.y);
    ctx.closePath();
    ctx.fill();
  }

  const drawTrail = () => {
    if (spatialTrail.length < 2) return;
    ctx.lineWidth = 2;
    ctx.beginPath();
    spatialTrail.forEach((pt, index) => {
      const p = polarToCanvas(cx, cy, scale, pt.bearing, pt.distance);
      if (index === 0) ctx.moveTo(p.x, p.y);
      else ctx.lineTo(p.x, p.y);
    });
    ctx.strokeStyle = "rgba(255,140,0,.48)";
    ctx.stroke();
    spatialTrail.forEach((pt, index) => {
      const age = index / Math.max(1, spatialTrail.length - 1);
      const p = polarToCanvas(cx, cy, scale, pt.bearing, pt.distance);
      ctx.fillStyle = `rgba(255,140,0,${0.15 + age * 0.35})`;
      ctx.beginPath();
      ctx.arc(p.x, p.y, 2 + age * 3, 0, Math.PI * 2);
      ctx.fill();
    });
  };

  const plotSensor = (angleDeg, distanceMm, label, options = {}) => {
    if (!positiveNumber(distanceMm)) return;
    const p = polarToCanvas(cx, cy, scale, angleDeg, distanceMm);
    const color = options.color || sensorDistanceColor(distanceMm);
    const radius = options.radius || 5;
    const glowRadius = radius * (3.2 + pulse);

    ctx.strokeStyle = options.ray || "rgba(255,255,255,.12)";
    ctx.lineWidth = options.rayWidth || 1;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(p.x, p.y);
    ctx.stroke();

    const glow = ctx.createRadialGradient(p.x, p.y, 0, p.x, p.y, glowRadius);
    glow.addColorStop(0, options.glow || "rgba(0,213,255,.26)");
    glow.addColorStop(1, "rgba(0,0,0,0)");
    ctx.fillStyle = glow;
    ctx.beginPath();
    ctx.arc(p.x, p.y, glowRadius, 0, Math.PI * 2);
    ctx.fill();

    ctx.fillStyle = color;
    ctx.beginPath();
    if (options.shape === "diamond") {
      ctx.moveTo(p.x, p.y - radius);
      ctx.lineTo(p.x + radius, p.y);
      ctx.lineTo(p.x, p.y + radius);
      ctx.lineTo(p.x - radius, p.y);
      ctx.closePath();
    } else {
      ctx.arc(p.x, p.y, radius, 0, Math.PI * 2);
    }
    ctx.fill();

    if (options.outline) {
      ctx.strokeStyle = "rgba(255,255,255,.72)";
      ctx.lineWidth = 1.5;
      ctx.stroke();
    }

    ctx.fillStyle = "rgba(255,255,255,.86)";
    ctx.font = options.bold ? "bold 11px system-ui" : "10px system-ui";
    ctx.textAlign = "center";
    ctx.textBaseline = "bottom";
    ctx.fillText(label, p.x, p.y - radius - 5);
    ctx.fillStyle = "rgba(255,255,255,.52)";
    ctx.font = "9px system-ui";
    ctx.textBaseline = "top";
    ctx.fillText(`${Math.round(distanceMm / 10) / 100}m`, p.x, p.y + radius + 4);
  };

  drawTrail();

  const lidar = state?.lidar || {};
  plotSensor(-42, lidar.front_left_mm, "雷达左", { color: "#7c5cff", glow: "rgba(124,92,255,.25)" });
  plotSensor(0, lidar.front_center_mm, "雷达中", { color: "#7c5cff", glow: "rgba(124,92,255,.25)" });
  plotSensor(42, lidar.front_right_mm, "雷达右", { color: "#7c5cff", glow: "rgba(124,92,255,.25)" });
  plotSensor(-100, lidar.side_left_mm, "雷达侧左", { color: "#7c5cff", glow: "rgba(124,92,255,.20)" });
  plotSensor(100, lidar.side_right_mm, "雷达侧右", { color: "#7c5cff", glow: "rgba(124,92,255,.20)" });

  const tof = state?.tof || {};
  if (channelValid(tof, "front_left_valid", "front_left_mm")) {
    plotSensor(-28, tof.front_left_mm, "TOF左", { color: "#5aa9ff", glow: "rgba(90,169,255,.24)" });
  }
  if (channelValid(tof, "front_center_valid", "front_center_mm")) {
    plotSensor(0, tof.front_center_mm, "TOF中", { color: "#5aa9ff", glow: "rgba(90,169,255,.24)" });
  }
  if (channelValid(tof, "front_right_valid", "front_right_mm")) {
    plotSensor(28, tof.front_right_mm, "TOF右", { color: "#5aa9ff", glow: "rgba(90,169,255,.24)" });
  }

  const ultrasonic = state?.ultrasonic || {};
  if (channelValid(ultrasonic, "left_valid", "left_mm")) {
    plotSensor(-90, ultrasonic.left_mm, "超声左", { color: "#1f8f8a", glow: "rgba(31,143,138,.25)" });
  }
  if (channelValid(ultrasonic, "right_valid", "right_mm")) {
    plotSensor(90, ultrasonic.right_mm, "超声右", { color: "#1f8f8a", glow: "rgba(31,143,138,.25)" });
  }

  const obstacle = state?.obstacle || {};
  plotSensor(-34, obstacle.front_left_mm, "融合左", { shape: "diamond", radius: 7, ray: "rgba(220,38,38,.18)", glow: "rgba(220,38,38,.22)" });
  plotSensor(0, obstacle.front_center_mm, "融合前", { shape: "diamond", radius: 7, ray: "rgba(220,38,38,.18)", glow: "rgba(220,38,38,.22)" });
  plotSensor(34, obstacle.front_right_mm, "融合右", { shape: "diamond", radius: 7, ray: "rgba(220,38,38,.18)", glow: "rgba(220,38,38,.22)" });
  plotSensor(-90, obstacle.side_left_mm, "融合侧左", { shape: "diamond", radius: 6, ray: "rgba(220,38,38,.12)", glow: "rgba(220,38,38,.18)" });
  plotSensor(90, obstacle.side_right_mm, "融合侧右", { shape: "diamond", radius: 6, ray: "rgba(220,38,38,.12)", glow: "rgba(220,38,38,.18)" });

  const uwb = state?.uwb || {};
  if (uwb.valid && positiveNumber(uwb.distance_mm)) {
    const bearing = Math.max(-120, Math.min(120, Number(uwb.bearing_deg || 0)));
    plotSensor(bearing, uwb.distance_mm, "UWB目标", {
      color: "#00d5ff",
      glow: "rgba(0,213,255,.38)",
      outline: true,
      radius: 8,
      bold: true,
      ray: "rgba(0,213,255,.28)",
      rayWidth: 1.5,
    });
  }

  const imu = state?.imu || {};
  const yaw = Number(imu.yaw_deg || 0);
  const yawRad = ((imu.valid ? yaw : 0) - 90) * Math.PI / 180;
  ctx.save();
  ctx.translate(cx, cy);
  ctx.rotate(yawRad);
  ctx.fillStyle = "#eef2f6";
  ctx.beginPath();
  ctx.moveTo(18, 0);
  ctx.lineTo(-12, -10);
  ctx.lineTo(-5, 0);
  ctx.lineTo(-12, 10);
  ctx.closePath();
  ctx.fill();
  ctx.restore();
  ctx.strokeStyle = imu.valid ? "rgba(0,213,255,.7)" : "rgba(255,255,255,.28)";
  ctx.lineWidth = 1.5;
  ctx.beginPath();
  ctx.arc(cx, cy, 16 + pulse * 3, 0, Math.PI * 2);
  ctx.stroke();

  const power = state?.power || {};
  const batteryOk = batteryVoltageSupported(power.battery_voltage) && !power.low_battery;
  ctx.fillStyle = "rgba(5,7,10,.72)";
  ctx.fillRect(8, 8, 128, 42);
  ctx.strokeStyle = "rgba(255,255,255,.14)";
  ctx.strokeRect(8, 8, 128, 42);
  ctx.fillStyle = batteryOk ? "#1aae39" : "#dc2626";
  ctx.fillRect(18, 23, Math.max(4, Math.min(64, batteryOk ? 64 : 18)), 10);
  ctx.strokeStyle = "rgba(255,255,255,.5)";
  ctx.strokeRect(18, 22, 68, 12);
  ctx.fillRect(88, 25, 3, 6);
  ctx.fillStyle = "rgba(255,255,255,.8)";
  ctx.font = "10px system-ui";
  ctx.textAlign = "left";
  ctx.textBaseline = "middle";
  ctx.fillText(batteryDisplayText(power.battery_voltage), 96, 28);

  ctx.fillStyle = "rgba(255,255,255,.34)";
  ctx.font = "10px system-ui";
  ctx.textAlign = "center";
  ctx.fillText("前", cx, cy - maxPx - 15);
  ctx.fillText("后", cx, cy + maxPx + 15);
  ctx.fillText("左", cx - maxPx - 15, cy);
  ctx.fillText("右", cx + maxPx + 15, cy);
}

// ── Canvas: UWB Polar ──

function drawUwb(uwb) {
  const canvas = els.uwbCanvas;
  if (!canvas) return;
  const ctx = canvas.getContext("2d");
  const w = canvas.width;
  const h = canvas.height;
  const cx = w / 2;
  const cy = h * 0.72;
  const r = Math.min(w * 0.42, h * 0.62);
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = "#091018";
  ctx.fillRect(0, 0, w, h);

  ctx.strokeStyle = "rgba(255,255,255,.16)";
  ctx.lineWidth = 1;
  for (let i = 1; i <= 3; i++) {
    ctx.beginPath();
    ctx.arc(cx, cy, (r / 3) * i, Math.PI, Math.PI * 2);
    ctx.stroke();
  }

  [-60, -30, 0, 30, 60].forEach((deg) => {
    const a = (-90 + deg) * Math.PI / 180;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(cx + Math.cos(a) * r, cy + Math.sin(a) * r);
    ctx.stroke();
  });

  ctx.fillStyle = "#d7dde8";
  ctx.beginPath();
  ctx.moveTo(cx, cy - 12);
  ctx.lineTo(cx - 10, cy + 10);
  ctx.lineTo(cx + 10, cy + 10);
  ctx.closePath();
  ctx.fill();

  if (uwb.valid && uwb.distance_mm > 0) {
    const dist = Math.min(uwb.distance_mm, MAX_RANGE_MM) / MAX_RANGE_MM;
    const deg = Math.max(-85, Math.min(85, uwb.bearing_deg || 0));
    const a = (-90 + deg) * Math.PI / 180;
    const x = cx + Math.cos(a) * r * dist;
    const y = cy + Math.sin(a) * r * dist;
    const glow = ctx.createRadialGradient(x, y, 0, x, y, 30);
    glow.addColorStop(0, "rgba(0,117,222,.38)");
    glow.addColorStop(1, "rgba(0,117,222,0)");
    ctx.fillStyle = glow;
    ctx.beginPath();
    ctx.arc(x, y, 30, 0, Math.PI * 2);
    ctx.fill();
    ctx.fillStyle = "#0075de";
    ctx.beginPath();
    ctx.arc(x, y, 8, 0, Math.PI * 2);
    ctx.fill();
  }
}

// ── Canvas: Obstacle Radar ──

function drawObstacle(obstacle) {
  const canvas = els.obstacleCanvas;
  if (!canvas) return;
  const ctx = canvas.getContext("2d");
  const w = canvas.width;
  const h = canvas.height;
  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = "#091018";
  ctx.fillRect(0, 0, w, h);

  const zones = [
    { key: "front_left_mm", label: "左前", x: w * 0.20 },
    { key: "front_center_mm", label: "正前", x: w * 0.50 },
    { key: "front_right_mm", label: "右前", x: w * 0.80 },
    { key: "side_left_mm", label: "左侧", x: w * 0.08 },
    { key: "side_right_mm", label: "右侧", x: w * 0.92 },
  ];

  ctx.strokeStyle = "rgba(255,255,255,.15)";
  ctx.lineWidth = 1;
  [500, 1000, 2000, 3000].forEach((mm) => {
    const y = h - 42 - (Math.min(mm, MAX_RANGE_MM) / MAX_RANGE_MM) * (h - 76);
    ctx.beginPath();
    ctx.moveTo(28, y);
    ctx.lineTo(w - 28, y);
    ctx.stroke();
    ctx.fillStyle = "rgba(255,255,255,.48)";
    ctx.font = "12px system-ui";
    ctx.fillText(`${mm / 1000}m`, 32, y - 4);
  });

  zones.forEach((z) => {
    const value = obstacle[z.key] || 0;
    const pct = value > 0 ? Math.max(0.05, Math.min(1, value / MAX_RANGE_MM)) : 0;
    const barH = pct * (h - 82);
    const x = z.x - 24;
    const y = h - 42 - barH;
    ctx.fillStyle = value > 0 && value < 500 ? "#d13b3b" : value > 0 && value < 1000 ? "#c98316" : "#0075de";
    ctx.globalAlpha = value > 0 ? 0.95 : 0.16;
    ctx.fillRect(x, y, 48, barH || 4);
    ctx.globalAlpha = 1;
    ctx.fillStyle = "#d7dde8";
    ctx.font = "13px system-ui";
    ctx.textAlign = "center";
    ctx.fillText(z.label, z.x, h - 18);
    ctx.fillText(value > 0 ? `${value}` : "--", z.x, Math.max(20, y - 8));
  });

  ctx.fillStyle = "#d7dde8";
  ctx.beginPath();
  ctx.moveTo(w / 2, h - 64);
  ctx.lineTo(w / 2 - 15, h - 36);
  ctx.lineTo(w / 2 + 15, h - 36);
  ctx.closePath();
  ctx.fill();
}

function animateSpatialMap(now) {
  if (els.spatialMap) {
    drawSpatialMap(latestState || {}, now);
  }
  requestAnimationFrame(animateSpatialMap);
}

// ── Network ──

async function postJson(path, body) {
  try {
    await fetch(path, {
      method: "POST",
      headers: authHeaders(),
      body: JSON.stringify(body),
    });
  } catch (e) {
    /* telemetry already shows offline */
  }
}

function connectWs() {
  if (!location.host) {
    setConn(false);
    return;
  }
  ws = new WebSocket(`ws://${location.host}/ws/state`);
  ws.onopen = () => {
    setConn(true);
    appendBrowserLog("WebSocket 已连接");
  };
  ws.onclose = () => {
    setConn(false);
    appendBrowserLog("WebSocket 已断开，准备重连", "W");
    setTimeout(connectWs, 1000);
  };
  ws.onerror = () => {
    appendBrowserLog("WebSocket 错误", "W");
    ws.close();
  };
  ws.onmessage = (ev) => {
    try {
      renderState(JSON.parse(ev.data));
    } catch (e) {
      appendBrowserLog("收到无法解析的状态帧", "W");
    }
  };
}

async function pollStateFallback() {
  if (Date.now() - lastStateAt < 2000) {
    return;
  }
  try {
    renderState(await fetchJsonWithTimeout("/api/state"));
    setConn(true);
    stateFallbackUnavailableLogged = false;
  } catch (e) {
    if (!stateFallbackUnavailableLogged) {
      appendBrowserLog(`/api/state 不可用：${e.name === "AbortError" ? "timeout" : e.message}`, "W");
      stateFallbackUnavailableLogged = true;
    }
  }
}

async function refreshLogs() {
  if (!els.logs) return;
  try {
    const body = await fetchJsonWithTimeout("/api/logs");
    if (Array.isArray(body.logs) && body.logs.length) {
      deviceLogs.splice(0, deviceLogs.length, ...body.logs.slice(-120));
    } else if (!deviceLogs.length && !browserLogs.length) {
      deviceLogs.splice(0, deviceLogs.length, "-- 暂无设备日志；等待传感器/视频/控制事件 --");
    }
    logsApiUnavailableLogged = false;
    renderLogs();
  } catch (e) {
    if (!logsApiUnavailableLogged) {
      appendBrowserLog(`/api/logs 不可用：${e.name === "AbortError" ? "timeout" : e.message}`, "W");
      logsApiUnavailableLogged = true;
    }
  }
}

// ── Joystick ──

function sendJog(deadman) {
  postJson("/api/jog", {
    seq: jogSeq++,
    forward: joyForward,
    turn: joyTurn,
    deadman,
    client_time_ms: Date.now(),
  });
}

function startJogLoop() {
  if (jogTimer) return;
  sendJog(true);
  jogTimer = setInterval(() => sendJog(true), 150);
}

function stopJogLoop() {
  if (jogTimer) {
    clearInterval(jogTimer);
    jogTimer = null;
  }
  joyForward = 0;
  joyTurn = 0;
  moveStick(0, 0);
  sendJog(false);
}

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

function setupJoystick() {
  if (!els.joy) return;
  els.joy.addEventListener("pointerdown", (ev) => {
    ev.preventDefault();
    joyPointerId = ev.pointerId;
    els.joy.setPointerCapture(joyPointerId);
    els.joy.classList.add("pressed");
    updateJoystick(ev);
    startJogLoop();
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
    stopJogLoop();
  };
  els.joy.addEventListener("pointerup", release);
  els.joy.addEventListener("pointercancel", release);
  els.joy.addEventListener("lostpointercapture", () => {
    els.joy.classList.remove("pressed");
    joyPointerId = null;
    stopJogLoop();
  });
}

// ── Safety: auto-stop jog when tab hidden / screen locked ──
document.addEventListener("visibilitychange", () => {
  if (document.hidden && jogTimer) stopJogLoop();
});
document.addEventListener("pagehide", () => { if (jogTimer) stopJogLoop(); });

// ── Event bindings ──

document.querySelectorAll(".fb-mode-btn").forEach((btn) => {
  btn.addEventListener("click", () =>
    postJson("/api/mode-request", { requested_mode: btn.dataset.mode })
  );
});

document.querySelectorAll(".fb-nav-btn").forEach((btn) => {
  btn.addEventListener("click", () => switchView(btn.dataset.view));
});

if (els.cameraStream) {
  els.cameraStream.addEventListener("load", () => {
    setCameraOnline(true, "画面在线");
    appendBrowserLog("视频画面在线");
    cameraRetryDelay = 3000;
    if (cameraRetryTimer) {
      clearTimeout(cameraRetryTimer);
      cameraRetryTimer = null;
    }
  });
  els.cameraStream.addEventListener("error", () => {
    setCameraOnline(false, "画面离线");
    appendBrowserLog("视频加载失败", "W");
    scheduleCameraRetry();
  });
}

document.addEventListener("fullscreenchange", handleFullscreenChange);
document.addEventListener("webkitfullscreenchange", handleFullscreenChange);
if (els.fullscreenBtn) {
  els.fullscreenBtn.addEventListener("click", (ev) => {
    ev.stopPropagation();
    toggleFullscreen();
  });
}

if (els.btnCameraReload) {
  els.btnCameraReload.addEventListener("click", () => {
    activeCameraUrl = "";
    saveCameraOverride(els.cameraUrl.value);
  });
}

if (extEls.resetFault) {
  extEls.resetFault.addEventListener("click", () => {
    postJson("/api/reset-fault", { confirm: true });
  });
}

if (extEls.btnSaveCal) {
  extEls.btnSaveCal.addEventListener("click", () => {
    const cal = {
      deadband_mv: parseInt(extEls.calDeadband.value, 10),
      min_active_mv: parseInt(extEls.calMinActive.value, 10),
      max_mv: parseInt(extEls.calMax.value, 10),
      module_full_scale_mv: parseInt(extEls.calFullScale.value, 10),
      rise_mv_per_s: parseInt(extEls.calRise.value, 10),
      fall_mv_per_s: parseInt(extEls.calFall.value, 10),
    };
    if (Object.values(cal).some((value) => Number.isNaN(value))) {
      alert("参数必须为有效整数。");
      return;
    }
    if (
      cal.deadband_mv < 0 ||
      cal.min_active_mv <= cal.deadband_mv ||
      cal.max_mv <= cal.min_active_mv ||
      cal.max_mv > cal.module_full_scale_mv
    ) {
      alert("参数关系有误：死区 < 起转 < 最大 <= 满量程。");
      return;
    }
    postJson("/api/calibrate", cal);
  });
}

if (extEls.btnWizardComplete) {
  extEls.btnWizardComplete.addEventListener("click", () => {
    const checks = {
      estop_checked: !!extEls.wizEstop?.checked,
      wheels_lifted: !!extEls.wizWheels?.checked,
      direction_checked: !!extEls.wizDirection?.checked,
      throttle_checked: !!extEls.wizThrottle?.checked,
    };
    if (
      !checks.estop_checked ||
      !checks.wheels_lifted ||
      !checks.direction_checked ||
      !checks.throttle_checked
    ) {
      alert("请先确认急停、架空、方向和油门标定步骤。");
      return;
    }
    postJson("/api/wizard-complete", { complete: true, ...checks });
  });
}

if (extEls.btnLocalKeySave) {
  extEls.btnLocalKeySave.addEventListener("click", () => {
    sessionStorage.setItem(LOCAL_KEY_STORAGE, extEls.localApiKey.value.trim());
    refreshLocalKeyUi();
    alert("本地控制 Key 已保存（当前标签页有效，关闭后需重新输入）。");
  });
}

// ── WiFi ──

async function refreshWifiStatus() {
  if (!wifiEls.status) return;
  try {
    const res = await fetch("/api/wifi/status");
    const st = await res.json();
    if (!st.provisioned) {
      wifiEls.status.textContent = "未配置";
      setTextState(wifiEls.status, false, true);
    } else if (st.sta_connected) {
      wifiEls.status.textContent = `${st.ssid} 已连接 (${st.rssi}dBm)`;
      setTextState(wifiEls.status, true);
    } else {
      wifiEls.status.textContent = `${st.ssid} 连接中`;
      setTextState(wifiEls.status, false, true);
    }
    wifiEls.ip.textContent = st.sta_connected && st.ip ? st.ip : "--";
  } catch (e) {
    /* keep last status */
  }
}

if (wifiEls.save) {
  wifiEls.save.addEventListener("click", async () => {
    const ssid = wifiEls.ssid.value.trim();
    const pass = wifiEls.pass.value;
    if (!ssid) {
      alert("请输入 WiFi 名称。");
      return;
    }
    if (pass.length > 0 && pass.length < 8) {
      alert("WiFi 密码至少 8 位（开放网络请留空）。");
      return;
    }
    wifiEls.save.disabled = true;
    wifiEls.save.textContent = "保存中";
    try {
      const res = await fetch("/api/wifi", {
        method: "POST",
        headers: authHeaders(),
        body: JSON.stringify({ ssid, password: pass }),
      });
      const ok = res.ok && (await res.json()).ok;
      alert(ok ? "已保存，小车正在连接该 WiFi。" : "保存失败，请检查输入。");
    } catch (e) {
      alert("请求失败，请确认已连接小车热点。");
    }
    wifiEls.save.disabled = false;
    wifiEls.save.textContent = "保存并连接";
    setTimeout(refreshWifiStatus, 3000);
  });
}

// ── Explicit-consent OTA ──

let otaAvailableVersion = "";
let otaPollTimer = null;

function renderOtaStatus(st) {
  otaEls.current.textContent = st.current_version || "--";
  otaEls.available.textContent = st.available_version || "--";
  otaAvailableVersion = st.update_available ? (st.available_version || "") : "";
  const busy = st.state === "installing" || st.state === "rebooting";
  otaEls.check.disabled = busy;
  otaEls.install.hidden = false;
  otaEls.install.disabled = busy || !otaAvailableVersion;
  otaEls.install.textContent = busy
    ? (st.state === "rebooting" ? "正在重启" : "正在安装")
    : (otaAvailableVersion ? "安装更新" : "暂无可安装更新");
  otaEls.later.hidden = !otaAvailableVersion || busy;

  const labels = {
    idle: "已是最新",
    checking: "检查中",
    update_available: "发现新版本",
    installing: "正在安装",
    rebooting: "校验完成，正在重启",
    failed: "更新检查/安装失败",
  };
  otaEls.state.textContent = labels[st.state] || "未检查";
  setTextState(otaEls.state, st.state === "idle", st.state === "checking");
  otaEls.hint.textContent = st.state === "failed"
    ? `失败：${st.reason || "未知原因"}。若安装已开始，车辆将保持安全停机，请受控重启或 USB 恢复。`
    : st.update_available
      ? `新版本 ${st.available_version} 可安装；不点击安装不会写入固件。`
      : "检查更新不会自动安装。";
}

async function refreshOtaStatus() {
  try {
    const res = await fetch("/api/ota/status", { cache: "no-store" });
    if (res.ok) renderOtaStatus(await res.json());
  } catch (e) {
    otaEls.state.textContent = "设备连接失败";
  }
}

function pollOtaStatus() {
  clearInterval(otaPollTimer);
  let remaining = 30;
  otaPollTimer = setInterval(async () => {
    await refreshOtaStatus();
    remaining -= 1;
    if (remaining <= 0) clearInterval(otaPollTimer);
  }, 1000);
}

otaEls.check?.addEventListener("click", async () => {
  otaEls.check.disabled = true;
  otaEls.install.disabled = true;
  otaEls.install.textContent = "检查中";
  otaEls.state.textContent = "提交检查请求";
  try {
    const res = await fetch("/api/ota/check", { method: "POST", headers: authHeaders() });
    if (!res.ok) throw new Error(res.status === 401 ? "本地控制 Key 无效" : "设备未联网或 OTA 忙");
    pollOtaStatus();
  } catch (e) {
    otaEls.state.textContent = "检查失败";
    otaEls.hint.textContent = e.message;
  } finally {
    otaEls.check.disabled = false;
  }
});

otaEls.install?.addEventListener("click", async () => {
  if (!otaAvailableVersion) return;
  if (!confirm(`安装固件 ${otaAvailableVersion}？安装期间车辆会强制停止并自动重启。`)) return;
  otaEls.install.disabled = true;
  try {
    const res = await fetch("/api/ota/install", {
      method: "POST",
      headers: authHeaders(),
      body: JSON.stringify({ version: otaAvailableVersion }),
    });
    if (!res.ok) throw new Error(res.status === 401 ? "本地控制 Key 无效" : "版本已变化，请重新检查");
    otaEls.state.textContent = "等待设备安装";
    otaEls.hint.textContent = "请勿断电；页面断开属于设备重启的预期现象。";
    pollOtaStatus();
  } catch (e) {
    otaEls.state.textContent = "安装请求失败";
    otaEls.hint.textContent = e.message;
    otaEls.install.disabled = false;
  }
});

otaEls.later?.addEventListener("click", () => {
  otaAvailableVersion = "";
  otaEls.install.hidden = false;
  otaEls.install.disabled = true;
  otaEls.install.textContent = "已暂不安装";
  otaEls.later.hidden = true;
  otaEls.state.textContent = "已暂不安装";
  otaEls.hint.textContent = "未提交安装请求；之后可再次检查。";
});

function uploadDirectOta() {
  const file = otaEls.directFile?.files?.[0];
  if (!file) {
    otaEls.directState.textContent = "未选择文件";
    otaEls.directHint.textContent = "请先选择 PlatformIO 生成的 firmware.bin。";
    return;
  }
  if (!file.name.endsWith(".bin")) {
    otaEls.directState.textContent = "文件类型不对";
    otaEls.directHint.textContent = "请选择 .bin 固件文件。";
    return;
  }
  if (!confirm(`直传 ${file.name}？上传期间车辆会强制停止并自动重启。`)) return;

  const form = new FormData();
  form.append("firmware", file, file.name);
  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/api/ota/local-upload");
  const key = localApiKey();
  if (key) xhr.setRequestHeader("X-FollowBox-Key", key);
  otaEls.directUpload.disabled = true;
  otaEls.directProgress.value = 0;
  otaEls.directState.textContent = "上传中";
  otaEls.directHint.textContent = "请勿断电；完成后页面断开属于设备重启的预期现象。";

  xhr.upload.onprogress = (event) => {
    if (event.lengthComputable) {
      otaEls.directProgress.value = Math.round((event.loaded / event.total) * 100);
    }
  };
  xhr.onload = () => {
    otaEls.directUpload.disabled = false;
    if (xhr.status >= 200 && xhr.status < 300) {
      otaEls.directProgress.value = 100;
      otaEls.directState.textContent = "重启中";
      otaEls.directHint.textContent = "上传完成，等待设备重新上线。";
      pollOtaStatus();
      return;
    }
    let reason = xhr.statusText || "上传失败";
    try {
      reason = JSON.parse(xhr.responseText).reason || reason;
    } catch (e) {
      /* keep HTTP text */
    }
    otaEls.directState.textContent = "上传失败";
    otaEls.directHint.textContent = xhr.status === 401 ? "本地控制 Key 无效。" : reason;
  };
  xhr.onerror = () => {
    otaEls.directUpload.disabled = false;
    otaEls.directState.textContent = "连接中断";
    otaEls.directHint.textContent = "若固件已写完，设备可能正在重启；否则请重新上传。";
  };
  xhr.send(form);
}

otaEls.directUpload?.addEventListener("click", uploadDirectOta);

refreshOtaStatus();

// ── Log actions ──
async function copyVisibleLogs() {
  const text = els.logs?.textContent || "";
  const trimmed = text.trim();
  const button = els.copyLogs;
  if (!button) return;
  if (!trimmed || trimmed === "-- 等待日志 --") {
    button.textContent = "无日志";
    setTimeout(() => { button.textContent = "复制"; }, 1200);
    return;
  }
  try {
    if (navigator.clipboard?.writeText) {
      await navigator.clipboard.writeText(text);
    } else {
      const textarea = document.createElement("textarea");
      textarea.value = text;
      textarea.setAttribute("readonly", "");
      textarea.style.position = "fixed";
      textarea.style.left = "-9999px";
      document.body.appendChild(textarea);
      textarea.select();
      const copied = document.execCommand("copy");
      document.body.removeChild(textarea);
      if (!copied) throw new Error("copy command failed");
    }
    button.textContent = "已复制";
  } catch (e) {
    button.textContent = "复制失败";
  }
  setTimeout(() => { button.textContent = "复制"; }, 1200);
}

if (els.clearLogs) {
  els.clearLogs.addEventListener("click", () => {
    deviceLogs.length = 0;
    browserLogs.length = 0;
    renderLogs();
  });
}
if (els.copyLogs) {
  els.copyLogs.addEventListener("click", copyVisibleLogs);
}

// ── Online/Offline detection ──

window.addEventListener("online", () => {
  if (!ws || ws.readyState > 1) connectWs();
});
window.addEventListener("offline", () => {
  setConn(false);
});

// ── Init ──

setInterval(pollStateFallback, 1000);
setInterval(refreshLogs, 2000);
setInterval(refreshWifiStatus, 5000);
refreshLogs();
refreshWifiStatus();
refreshLocalAuthStatus();
refreshLocalKeyUi();
restoreCameraOverride();
setCameraStream(els.cameraUrl?.value);
setupJoystick();
setupCanvasDPI(els.spatialMap);
setupCanvasDPI(els.uwbCanvas);
setupCanvasDPI(els.obstacleCanvas);
drawSpatialMap({});
drawUwb({});
drawObstacle({});
requestAnimationFrame(animateSpatialMap);
const initialView = (location.hash || "").slice(1);
if (["drive", "sensors", "status", "settings"].includes(initialView)) {
  switchView(initialView);
}
connectWs();
