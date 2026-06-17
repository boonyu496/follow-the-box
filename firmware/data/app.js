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
  obstacle: $("obstacle"),
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
  tofLeftBar: $("tof-left-bar"),
  tofCenterBar: $("tof-center-bar"),
  tofRightBar: $("tof-right-bar"),
  ultrasonic: $("ultrasonic"),
  ultraLeft: $("ultra-left"),
  ultraRight: $("ultra-right"),
  camera: $("camera"),
  cameraLink: $("camera-link"),
  cameraStream: $("camera-stream"),
  cameraPlaceholder: $("camera-placeholder"),
  cameraStatus: $("camera-status"),
  cameraUrl: $("camera-url"),
  cameraUrlState: $("camera-url-state"),
  btnCameraReload: $("btn-camera-reload"),
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

// modeLabels / stopLabels → loaded from ../shared/helpers.js

const LOCAL_KEY_STORAGE = "followbox.localApiKey"; // sessionStorage — cleared on tab close
const CAMERA_URL_STORAGE = "followbox.cameraStreamUrl";
const MAX_RANGE_MM = 3000;

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
  latestUwbData = u;

  const o = s.obstacle ?? {};
  const hasFrontObstacle = positiveNumber(o.front_left_mm) ||
    positiveNumber(o.front_center_mm) || positiveNumber(o.front_right_mm);
  const hasSideObstacle = positiveNumber(o.side_left_mm) || positiveNumber(o.side_right_mm);
  els.obstacle.textContent = hasFrontObstacle || !hasSideObstacle
    ? `${fmtMm(o.front_left_mm)} / ${fmtMm(o.front_center_mm)} / ${fmtMm(o.front_right_mm)}`
    : `侧 ${fmtMm(o.side_left_mm)} / ${fmtMm(o.side_right_mm)}`;
  latestObstacleData = o;

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
  setBar("left", tof.front_left_mm, channelValid(tof, "front_left_valid", "front_left_mm"));
  setBar("center", tof.front_center_mm, channelValid(tof, "front_center_valid", "front_center_mm"));
  setBar("right", tof.front_right_mm, channelValid(tof, "front_right_valid", "front_right_mm"));

  const us = s.ultrasonic ?? {};
  const usValidCount = [
    channelValid(us, "left_valid", "left_mm"),
    channelValid(us, "right_valid", "right_mm"),
  ].filter(Boolean).length;
  els.ultrasonic.textContent = validityLabel(usValidCount, 2);
  setTextState(els.ultrasonic, usValidCount === 2, usValidCount > 0);
  els.ultraLeft.textContent = channelMm(us, "left_valid", "left_mm");
  els.ultraRight.textContent = channelMm(us, "right_valid", "right_mm");

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
  if (cam.stream_url) {
    lastTelemetryCameraUrl = cam.stream_url;
    if (!userCameraOverride) setCameraStream(cam.stream_url);
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

// ── Online/Offline detection ──
window.addEventListener("online", () => {
  if (!ws || ws.readyState > 1) connectWs();
});
window.addEventListener("offline", () => {
  setConn(false);
});

// ── Init ──

setInterval(refreshWifiStatus, 5000);
refreshWifiStatus();
refreshLocalKeyUi();
restoreCameraOverride();
setCameraStream(els.cameraUrl?.value);
setupJoystick();
setupCanvasDPI(els.uwbCanvas);
setupCanvasDPI(els.obstacleCanvas);
drawUwb({});
drawObstacle({});
connectWs();
