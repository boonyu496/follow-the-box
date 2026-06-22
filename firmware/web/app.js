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
  btnCameraReload: $("btn-camera-reload"),
  logs: $("logs"),
  clearLogs: $("clear-logs"),
  joy: $("joy"),
  stick: $("stick"),
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
};

// modeLabels / stopLabels → loaded from ../shared/helpers.js

const LOCAL_KEY_STORAGE = "followbox.localApiKey"; // sessionStorage — cleared on tab close
const CAMERA_URL_STORAGE = "followbox.cameraStreamUrl";
const MAX_RANGE_MM = 3000;
const TOF_RATE_WINDOW_MS = 5000;

let ws = null;
let jogSeq = 1;
let jogTimer = null;
let joyPointerId = null;
let joyForward = 0;
let joyTurn = 0;
let activeCameraUrl = "";
let lastTelemetryCameraUrl = "";
let userCameraOverride = false;
let latestState = null;
let lastStateAt = 0;
const tofRateWindow = [];

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
  extEls.localKeyStatus.textContent = key ? "已设置" : "未设置";
  setTextState(extEls.localKeyStatus, !!key, !key);
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

function setConn(online) {
  els.conn.textContent = online ? "已连接" : "未连接";
  els.conn.classList.toggle("fb-pill--ok", online);
  els.conn.classList.toggle("fb-pill--danger", !online);
}

function setCameraStream(url) {
  if (!els.cameraStream || !els.cameraUrl) return;
  const next = (url || els.cameraUrl.value || "").trim();
  if (!next || next === activeCameraUrl) return;
  activeCameraUrl = next;
  els.cameraUrl.value = next;
  if (els.cameraUrlState) els.cameraUrlState.textContent = next;
  els.cameraStatus.textContent = "加载中";
  els.cameraStream.src = next;
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

function setCameraOnline(online, text) {
  if (els.cameraPlaceholder) els.cameraPlaceholder.classList.toggle("hidden", online);
  if (els.cameraStream) els.cameraStream.classList.toggle("online", online);
  if (els.cameraStatus) els.cameraStatus.textContent = text;
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
      setupCanvasDPI(els.uwbCanvas);
      setupCanvasDPI(els.obstacleCanvas);
      drawUwb(latestState.uwb || {});
      drawObstacle(latestState.obstacle || {});
    }
  });
}

// ── Render ──

function renderState(s) {
  latestState = s;
  lastStateAt = Date.now();
  const mode = s.mode ?? "--";
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
  const batteryPct = estimateBatteryPercent(p.battery_voltage);
  els.battery.textContent =
    p.battery_voltage != null
      ? `${p.battery_voltage.toFixed(1)}V ${Math.round(batteryPct)}%`
      : "--";
  setTextState(els.battery, !p.low_battery, p.low_battery);

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
    lidarDiagnosis = "无串口数据：检查供电、雷达 TX→GPIO3 和共地";
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
  if (tofValidCount > 0) {
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
  els.rcAge.textContent = fmtAge(s.now_ms, rc.last_update_ms);
  setTextState(els.rc, !!rc.online, !rc.online);

  const cloud = s.cloud ?? {};
  els.cloud.textContent = cloud.connected ? `seq ${cloud.last_seq ?? 0}` : "离线";
  els.cloudAge.textContent = fmtAge(s.now_ms, cloud.last_update_ms);
  setTextState(els.cloud, !!cloud.connected, !cloud.connected);

  const cam = s.camera ?? {};
  els.camera.textContent = cam.online ? "摄像头在线" : "摄像头离线";
  els.cameraLink.textContent = cam.online ? "在线" : "离线";
  setTextState(els.cameraLink, !!cam.online, !cam.online);
  setStatus(els.sensorCameraStatus, cam.online ? "在线" : "离线", !!cam.online, !cam.online);
  if (els.sensorCameraAge) els.sensorCameraAge.textContent = cam.stream_url ? "有地址" : "无地址";
  if (els.sensorCameraDetail) els.sensorCameraDetail.textContent = cam.online ? "在线" : "离线";
  setStatus(els.sensorPowerStatus, p.low_battery ? "低电压" : (p.battery_voltage != null ? "正常" : "无数据"),
            !p.low_battery && p.battery_voltage != null, p.battery_voltage != null);
  if (els.sensorPowerAge) els.sensorPowerAge.textContent = p.battery_voltage != null ? `${p.battery_voltage.toFixed(2)}V` : "未更新";
  if (els.sensorBatteryDetail) {
    els.sensorBatteryDetail.textContent =
      p.battery_voltage != null ? `${p.battery_voltage.toFixed(2)}V / ${Math.round(batteryPct)}%` : "--";
  }
  if (els.sensorAuxStatus) {
    els.sensorAuxStatus.textContent = `${p.low_battery ? "电池告警" : "电池正常"} / ${cam.online ? "视频在线" : "视频离线"}`;
  }
  if (cam.stream_url) {
    lastTelemetryCameraUrl = cam.stream_url;
    if (!userCameraOverride) setCameraStream(cam.stream_url);
  }

  const sensorOkCount = [
    !!u.valid,
    imuValid,
    !!lidar.valid,
    tofValidCount > 0,
    usValidCount > 0,
    !!cam.online,
    p.battery_voltage != null && !p.low_battery,
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
  ws.onopen = () => setConn(true);
  ws.onclose = () => {
    setConn(false);
    setTimeout(connectWs, 1000);
  };
  ws.onerror = () => ws.close();
  ws.onmessage = (ev) => {
    try {
      renderState(JSON.parse(ev.data));
    } catch (e) {
      /* ignore malformed frame */
    }
  };
}

async function pollStateFallback() {
  if (Date.now() - lastStateAt < 2000) {
    return;
  }
  try {
    const res = await fetch("/api/state", { cache: "no-store" });
    if (!res.ok) return;
    renderState(await res.json());
    setConn(true);
  } catch (e) {
    /* WebSocket reconnect owns the offline indicator. */
  }
}

async function refreshLogs() {
  if (!els.logs) return;
  try {
    const res = await fetch("/api/logs", { cache: "no-store" });
    if (!res.ok) return;
    const body = await res.json();
    if (Array.isArray(body.logs) && body.logs.length) {
      els.logs.textContent = body.logs.slice(-120).join("\n");
    }
  } catch (e) {
    /* keep last visible logs */
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
  els.cameraStream.addEventListener("load", () => setCameraOnline(true, "画面在线"));
  els.cameraStream.addEventListener("error", () => setCameraOnline(false, "画面离线"));
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

refreshOtaStatus();

// ── Online/Offline detection ──
if (els.clearLogs) {
  els.clearLogs.addEventListener("click", () => {
    els.logs.textContent = "";
  });
}

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
refreshLocalKeyUi();
restoreCameraOverride();
setCameraStream(els.cameraUrl?.value);
setupJoystick();
setupCanvasDPI(els.uwbCanvas);
setupCanvasDPI(els.obstacleCanvas);
drawUwb({});
drawObstacle({});
const initialView = (location.hash || "").slice(1);
if (["drive", "sensors", "status", "settings"].includes(initialView)) {
  switchView(initialView);
}
connectWs();
