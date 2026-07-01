const DEVICE_ONLINE_TTL_MS = 10000;
const MAX_DEVICE_LOG_LINES = 160;
const MAX_BROADCAST_LOG_LINES = 60;
const MAX_LOG_LINE_CHARS = 512;

const devices = new Map();

function nowIso() {
  return new Date().toISOString();
}

function getDevice(id) {
  if (!devices.has(id)) {
    // seq is seeded from wall-clock seconds so it keeps increasing across
    // server restarts (the firmware also tolerates resets, belt and braces).
    const seqBase = Math.floor(Date.now() / 1000);
    devices.set(id, {
      id,
      seq: seqBase,
      command: { seq: seqBase, deadman: false, forward: 0, turn: 0, safe_idle: false },
      commandAt: 0,
      lastIngestAt: 0,
      firmwareVersion: "",
      state: null,
      logs: [],
      ota: {
        requestId: "",
        requestedVersion: "",
        status: "idle",
        requestedAt: 0,
        updatedAt: 0,
        reason: "",
      },
      duplicateFirmwareLogAt: 0,
      video: {
        frame: null,
        frameSeq: 0,
        lastFrameAt: 0,
        contentType: "image/jpeg",
      },
    });
  }
  return devices.get(id);
}

function sanitizeLogLine(line) {
  return String(line).slice(0, MAX_LOG_LINE_CHARS);
}

function appendDeviceLogs(device, incoming) {
  if (!Array.isArray(incoming) || incoming.length === 0) return;
  const recent = new Set(device.logs.slice(-MAX_DEVICE_LOG_LINES));
  for (const raw of incoming.slice(-50)) {
    const line = sanitizeLogLine(raw);
    if (!line || recent.has(line)) continue;
    device.logs.push(line);
    recent.add(line);
  }
  device.logs = device.logs.slice(-MAX_DEVICE_LOG_LINES);
}

function buildBroadcastPayload(device, now = Date.now()) {
  return {
    id: device.id,
    at: nowIso(),
    lastIngestAt: device.lastIngestAt,
    online: device.lastIngestAt > 0 && now - device.lastIngestAt < DEVICE_ONLINE_TTL_MS,
    state: device.state,
    logs: device.logs.slice(-MAX_BROADCAST_LOG_LINES),
    command: device.command,
    commandAt: device.commandAt,
    ota: device.ota,
    video: {
      lastFrameAt: device.video.lastFrameAt,
      frameSeq: device.video.frameSeq,
      online: Date.now() - device.video.lastFrameAt < 5000,
    },
  };
}

function currentFirmwareVersion(device) {
  return String(device.state?.firmware?.version || device.firmwareVersion || "");
}

function reportedFirmwareVersion(state) {
  return String(state?.firmware?.version || "").slice(0, 64);
}

function nextStop(device) {
  device.seq += 1;
  device.command = { seq: device.seq, deadman: false, forward: 0, turn: 0, safe_idle: false };
  device.commandAt = Date.now();
  return device.command;
}

module.exports = {
  DEVICE_ONLINE_TTL_MS,
  MAX_BROADCAST_LOG_LINES,
  MAX_DEVICE_LOG_LINES,
  MAX_LOG_LINE_CHARS,
  appendDeviceLogs,
  buildBroadcastPayload,
  currentFirmwareVersion,
  getDevice,
  nextStop,
  nowIso,
  reportedFirmwareVersion,
};
