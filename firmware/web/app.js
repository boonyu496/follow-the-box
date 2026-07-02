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
  netMode: $("wifi-net-mode"),
  modeHint: $("wifi-mode-hint"),
  btnHotspot: $("btn-net-hotspot"),
  btnLink: $("btn-net-link"),
};

const reconnectEls = {
  banner: $("reconnect-banner"),
  text: $("reconnect-text"),
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
// telemetry display helpers → loaded from ./js/telemetry.js

function setConn(online) {
  els.conn.textContent = online ? "已连接" : "未连接";
  els.conn.classList.toggle("fb-pill--ok", online);
  els.conn.classList.toggle("fb-pill--danger", !online);
  if (reconnectEls.banner) {
    reconnectEls.banner.hidden = online;
  }
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
  if (!CAMERA_ENABLED) {
    stopCloudVideoRelay();
    els.cameraStream.removeAttribute("src");
    activeCameraUrl = "";
    setCameraVisible(false, "摄像头已停用");
    return;
  }
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
  if (!CAMERA_ENABLED) return;
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
