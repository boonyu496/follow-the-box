const $ = (id) => document.getElementById(id);

const els = {
  status: $("status"),
  deviceId: $("device-id"),
  operatorToken: $("operator-token"),
  mode: $("mode"),
  stop: $("stop"),
  motion: $("motion"),
  battery: $("battery"),
  uwb: $("uwb"),
  motor: $("motor"),
  tofCenter: $("tof-center"),
  tofFl: $("tof-fl"),
  tofFr: $("tof-fr"),
  ultrasonicLeft: $("ultrasonic-left"),
  ultrasonicRight: $("ultrasonic-right"),
  obstacleCenter: $("obstacle-center"),
  obstacleFl: $("obstacle-fl"),
  obstacleFr: $("obstacle-fr"),
  forwardVal: $("forward-val"),
  turnVal: $("turn-val"),
  deadman: $("deadman"),
  safeIdle: $("safe-idle"),
  logs: $("logs"),
  raw: $("raw"),
  camStatus: $("cam-status"),
  camStream: $("cam-stream"),
  camUrl: $("cam-url"),
  joystickArea: $("joystick-area"),
  joystickKnob: $("joystick-knob"),
  cameraWrapper: $("camera-wrapper"),
  fullscreenBtn: $("fullscreen-btn"),
  clearLogs: $("clear-logs"),
};

const DEFAULT_CAMERA_STREAM = "http://192.168.4.2:81/stream";

let events = null;
let jogTimer = null;
let activeTab = "telemetry";
let isFullscreen = false;
let activeCameraUrl = "";
let userCameraOverride = false;

// ── Joystick State ──

let joyForward = 0;
let joyTurn = 0;

// ── Fullscreen Video ──

function toggleFullscreen() {
  const wrapper = els.cameraWrapper;
  if (!isFullscreen) {
    if (wrapper.requestFullscreen) {
      wrapper.requestFullscreen();
    } else if (wrapper.webkitRequestFullscreen) {
      wrapper.webkitRequestFullscreen();
    } else if (wrapper.msRequestFullscreen) {
      wrapper.msRequestFullscreen();
    }
  } else {
    if (document.exitFullscreen) {
      document.exitFullscreen();
    } else if (document.webkitExitFullscreen) {
      document.webkitExitFullscreen();
    } else if (document.msExitFullscreen) {
      document.msExitFullscreen();
    }
  }
}

function handleFullscreenChange() {
  isFullscreen = document.fullscreenElement !== null;
  els.fullscreenBtn.textContent = isFullscreen ? "⛶" : "⛶";
  setTimeout(initJoystick, 100);
}

document.addEventListener("fullscreenchange", handleFullscreenChange);
document.addEventListener("webkitfullscreenchange", handleFullscreenChange);

els.fullscreenBtn.addEventListener("click", (e) => {
  e.stopPropagation();
  toggleFullscreen();
});

els.camStream.addEventListener("click", () => {
  toggleFullscreen();
});

// ── Single Joystick (Cross Direction) ──

function initJoystick() {
  const area = els.joystickArea;
  const knob = els.joystickKnob;
  if (!area || !knob) return;

  let dragging = false;
  let cx = 0, cy = 0;
  let radius = 0;

  function getCenter() {
    const rect = area.getBoundingClientRect();
    cx = rect.width / 2;
    cy = rect.height / 2;
    radius = Math.min(cx, cy);
  }

  function handleMove(clientX, clientY) {
    const rect = area.getBoundingClientRect();
    let dx = clientX - (rect.left + cx);
    let dy = -(clientY - (rect.top + cy));

    const dist = Math.sqrt(dx * dx + dy * dy);
    if (dist > radius) {
      dx = (dx / dist) * radius;
      dy = (dy / dist) * radius;
    }

    // Update knob position - Y轴方向修正
    knob.style.left = `${cx + dx}px`;
    knob.style.top = `${cy - dy}px`;

    // Normalize: -1 to 1
    joyForward = Math.max(-1, Math.min(1, dy / radius));
    joyTurn = Math.max(-1, Math.min(1, dx / radius));

    // Update display
    if (els.forwardVal) els.forwardVal.textContent = Math.round(joyForward * 100);
    if (els.turnVal) els.turnVal.textContent = Math.round(joyTurn * 100);

    // Send command if jogging
    if (jogTimer) sendCommand(jogBody());
  }

  function resetKnob() {
    knob.style.left = `calc(50% - 25px)`;
    knob.style.top = `calc(50% - 25px)`;
  }

  // Mouse events
  area.addEventListener("mousedown", (e) => {
    dragging = true;
    getCenter();
    handleMove(e.clientX, e.clientY);
    area.classList.add("active");
    e.preventDefault();
  });
  document.addEventListener("mousemove", (e) => {
    if (!dragging) return;
    handleMove(e.clientX, e.clientY);
  });
  document.addEventListener("mouseup", () => {
    if (dragging) {
      dragging = false;
      area.classList.remove("active");
      resetKnob();
    }
  });

  // Touch events
  area.addEventListener("touchstart", (e) => {
    dragging = true;
    getCenter();
    handleMove(e.touches[0].clientX, e.touches[0].clientY);
    area.classList.add("active");
    e.preventDefault();
  }, { passive: false });
  document.addEventListener("touchmove", (e) => {
    if (!dragging) return;
    handleMove(e.touches[0].clientX, e.touches[0].clientY);
    e.preventDefault();
  }, { passive: false });
  document.addEventListener("touchend", () => {
    if (dragging) {
      dragging = false;
      area.classList.remove("active");
      resetKnob();
    }
  });

  // Init position
  setTimeout(() => {
    getCenter();
    resetKnob();
  }, 100);
}

// ── Tab Navigation ──

function switchTab(tabName) {
  activeTab = tabName;
  document.querySelectorAll(".tab").forEach((t) => {
    const isActive = t.dataset.tab === tabName;
    t.classList.toggle("active", isActive);
    t.setAttribute("aria-selected", isActive);
  });
  document.querySelectorAll(".tab-panel").forEach((p) => {
    p.classList.toggle("active", p.id === `panel-${tabName}`);
  });

  if (tabName === "control") {
    setTimeout(initJoystick, 50);
  }
}

document.querySelectorAll(".tab").forEach((tab) => {
  tab.addEventListener("click", () => switchTab(tab.dataset.tab));
});

// ── Device ID ──

function deviceId() {
  return encodeURIComponent(els.deviceId.value.trim() || "followbox-001");
}

// ── Status Badge ──

function setOnline(online) {
  els.status.textContent = online ? "已连接" : "未连接";
  els.status.classList.toggle("on", online);
  els.status.classList.toggle("off", !online);
}

// ── Telemetry Render ──

function render(payload) {
  setOnline(true);
  const s = payload.state || {};
  const safety = s.safety || {};
  const power = s.power || {};
  const uwb = s.uwb || {};
  const motor = s.motor || {};
  const tof = s.tof || {};
  const ultrasonic = s.ultrasonic || {};
  const obstacle = s.obstacle || {};
  const camera = s.camera || {};

  // Core stats
  els.mode.textContent = s.mode || "--";
  els.stop.textContent = safety.stop_reason || "--";
  els.motion.textContent = safety.motion_allowed ? "允许" : "禁止";
  els.battery.textContent =
    power.battery_voltage != null ? `${Number(power.battery_voltage).toFixed(1)}V` : "--";
  els.uwb.textContent = uwb.valid ? `${uwb.distance_mm}mm ${uwb.bearing_deg}°` : "无效";
  els.motor.textContent = `${motor.enable ? "使能" : "关"} L${motor.left_target || 0} R${motor.right_target || 0}`;

  // TOF
  const tofV = tof.valid ? tof : {};
  els.tofCenter.textContent = tofV.front_center_mm != null && tof.valid ? `${tofV.front_center_mm}` : "--";
  els.tofFl.textContent = tofV.front_left_mm != null && tof.valid ? `${tofV.front_left_mm}` : "--";
  els.tofFr.textContent = tofV.front_right_mm != null && tof.valid ? `${tofV.front_right_mm}` : "--";

  // Ultrasonic
  els.ultrasonicLeft.textContent = ultrasonic.left_mm != null && ultrasonic.valid ? `${ultrasonic.left_mm}` : "--";
  els.ultrasonicRight.textContent = ultrasonic.right_mm != null && ultrasonic.valid ? `${ultrasonic.right_mm}` : "--";

  // Obstacle
  els.obstacleCenter.textContent = obstacle.front_center_mm != null ? `${obstacle.front_center_mm}` : "--";
  els.obstacleFl.textContent = obstacle.front_left_mm != null ? `${obstacle.front_left_mm}` : "--";
  els.obstacleFr.textContent = obstacle.front_right_mm != null ? `${obstacle.front_right_mm}` : "--";

  if (camera.stream_url && (!userCameraOverride || !activeCameraUrl)) {
    updateCamStream(camera.stream_url, { fromTelemetry: true });
  } else if (!activeCameraUrl) {
    updateCamStream(DEFAULT_CAMERA_STREAM, { fromTelemetry: true });
  }

  // Raw JSON
  els.raw.textContent = JSON.stringify(s, null, 2);

  // Logs
  if (payload.logs && payload.logs.length) {
    els.logs.textContent = payload.logs.slice(-120).join("\n");
  }
}

// ── EventSource (SSE) ──

function connectEvents() {
  if (events) events.close();
  setOnline(false);
  // /events requires the operator token (EventSource cannot set headers).
  const token = encodeURIComponent(els.operatorToken.value.trim());
  events = new EventSource(`/api/device/${deviceId()}/events?token=${token}`);
  events.onmessage = (ev) => render(JSON.parse(ev.data));
  events.onerror = () => setOnline(false);
}

// Reconnect the telemetry stream when device id or token changes.
els.deviceId.addEventListener("change", connectEvents);
els.operatorToken.addEventListener("change", connectEvents);

// ── Command API ──

async function sendCommand(body) {
  const headers = { "Content-Type": "application/json" };
  const token = els.operatorToken.value.trim();
  if (token) headers.Authorization = `Bearer ${token}`;
  try {
    await fetch(`/api/device/${deviceId()}/command`, {
      method: "POST",
      headers,
      body: JSON.stringify(body),
    });
  } catch (e) {
    // Network error — don't break UI
  }
}

// ── Jog via Joystick ──

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
  sendCommand(jogBody());
  if (els.forwardVal) els.forwardVal.textContent = "0";
  if (els.turnVal) els.turnVal.textContent = "0";
  if (els.joystickKnob) {
    els.joystickKnob.style.left = "";
    els.joystickKnob.style.top = "";
  }
}

els.deadman.addEventListener("mousedown", (e) => {
  e.preventDefault();
  startJog();
});
els.deadman.addEventListener("mouseup", (e) => {
  e.preventDefault();
  stopJog();
});
els.deadman.addEventListener("mouseleave", stopJog);
els.deadman.addEventListener("touchstart", (e) => {
  e.preventDefault();
  startJog();
});
els.deadman.addEventListener("touchend", (e) => {
  e.preventDefault();
  stopJog();
});

els.safeIdle.addEventListener("click", () =>
  sendCommand({ safe_idle: true, deadman: false, forward: 0, turn: 0 })
);

// ── Camera ──

function updateCamStream(urlOverride, options = {}) {
  const url = (urlOverride || els.camUrl.value || DEFAULT_CAMERA_STREAM).trim();
  if (!url || url === activeCameraUrl) return;
  activeCameraUrl = url;
  els.camStream.src = url;
  els.camUrl.value = url;
  els.camStatus.textContent = "加载中…";
  els.camStatus.className = "camera-badge";
  els.camStream.onload = () => {
    els.camStatus.textContent = "在线";
    els.camStatus.className = "camera-badge on";
  };
  els.camStream.onerror = () => {
    els.camStatus.textContent = "离线";
  };
}

els.camUrl.addEventListener("input", () => {
  userCameraOverride = true;
});
els.camUrl.addEventListener("change", () => updateCamStream(els.camUrl.value));

// ── Clear Logs ──

els.clearLogs.addEventListener("click", () => {
  els.logs.textContent = "";
});

// ── Init ──

connectEvents();
updateCamStream(DEFAULT_CAMERA_STREAM, { fromTelemetry: true });
