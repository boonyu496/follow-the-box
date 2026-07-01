// FollowBox embedded H5 telemetry helpers.
// Pure display/math helpers only; app.js owns DOM wiring and requests.

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
