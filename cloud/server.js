const http = require("http");
const fs = require("fs");
const path = require("path");
const { createDeviceRoutes } = require("./routes/device");
const { createFirmwareRoutes } = require("./routes/firmware");
const { createHealthRoutes } = require("./routes/health");
const { createFirmwareManifestStore } = require("./services/firmware_manifest");
const {
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
} = require("./services/device_store");

const PORT = Number(process.env.PORT || 8080);
const DEVICE_TOKEN = process.env.FOLLOWBOX_DEVICE_TOKEN || "";
const OPERATOR_TOKEN = process.env.FOLLOWBOX_OPERATOR_TOKEN || "";
const ALLOW_INSECURE_DEV_AUTH =
  process.env.FOLLOWBOX_ALLOW_INSECURE_DEV_AUTH === "1";
const PUBLIC_DIR = path.join(__dirname, "public");
const DEPLOY_VERSION_FILE = path.join(PUBLIC_DIR, "deploy-version.txt");
const FIRMWARE_DIR = path.join(__dirname, "firmware");
const COMMAND_TTL_MS = 750;
const DUPLICATE_FIRMWARE_HOLD_MS = DEVICE_ONLINE_TTL_MS * 3;
const SSE_HEARTBEAT_MS = 15000;

const clients = new Set();
const firmwareManifestStore = createFirmwareManifestStore({
  firmwareDir: FIRMWARE_DIR,
  deviceToken: DEVICE_TOKEN,
});
const readFirmwareManifest = firmwareManifestStore.read;

function validDeviceToken(body, url) {
  const token = body?.token || url.searchParams.get("token") || "";
  if (DEVICE_TOKEN) return token === DEVICE_TOKEN;
  return ALLOW_INSECURE_DEV_AUTH;
}

function validOperator(req) {
  const auth = req.headers.authorization || "";
  if (OPERATOR_TOKEN) return auth === `Bearer ${OPERATOR_TOKEN}`;
  return ALLOW_INSECURE_DEV_AUTH;
}

function validOperatorQuery(url) {
  const token = url.searchParams.get("token") || "";
  if (OPERATOR_TOKEN) return token === OPERATOR_TOKEN;
  return ALLOW_INSECURE_DEV_AUTH;
}

function broadcast(device) {
  const event = JSON.stringify(buildBroadcastPayload(device));
  // Only push to subscribers of THIS device; never leak other devices' data.
  for (const client of clients) {
    if (client.deviceId === device.id) {
      client.res.write(`data: ${event}\n\n`);
    }
  }
}

function deployVersion() {
  try {
    const text = fs.readFileSync(DEPLOY_VERSION_FILE, "utf8").trim();
    return text || String(Date.now());
  } catch (err) {
    return String(Date.now());
  }
}

function healthSummary() {
  return {
    ok: true,
    service: "followbox-cloud",
    at: nowIso(),
    version: deployVersion(),
    firmware_manifest: firmwareManifestStore.exists(),
  };
}

function serveStatic(req, res) {
  const url = new URL(req.url, `http://${req.headers.host}`);
  const file = url.pathname === "/" ? "index.html" : url.pathname.slice(1);
  const full = path.normalize(path.join(PUBLIC_DIR, file));
  if (!full.startsWith(PUBLIC_DIR)) {
    send(res, 403, "forbidden", "text/plain");
    return;
  }
  fs.readFile(full, (err, data) => {
    if (err) {
      send(res, 404, "not found");
      return;
    }
    const mime = file.endsWith(".css") ? "text/css" :
                 file.endsWith(".js") ? "application/javascript" :
                 file.endsWith(".html") ? "text/html" : "application/octet-stream";
    if (file === "index.html") {
      const version = encodeURIComponent(deployVersion());
      data = Buffer.from(
        data.toString("utf8").replaceAll("__FB_DEPLOY_VERSION__", version),
        "utf8"
      );
    }
    send(res, 200, data, mime);
  });
}

function shouldIgnoreDuplicateFirmwareReport(deviceId, device, reportedVersion, now = Date.now()) {
  const manifest = readFirmwareManifest(deviceId);
  const activeVersion = currentFirmwareVersion(device);
  if (!manifest || !reportedVersion || reportedVersion === manifest.version) return false;
  return activeVersion === manifest.version &&
    device.lastIngestAt > 0 &&
    now - device.lastIngestAt < DUPLICATE_FIRMWARE_HOLD_MS;
}

function noteIgnoredDuplicateFirmwareReport(deviceId, device, reportedVersion) {
  const now = Date.now();
  if (device.duplicateFirmwareLogAt && now - device.duplicateFirmwareLogAt < 10000) return;
  device.duplicateFirmwareLogAt = now;
  const activeVersion = currentFirmwareVersion(device);
  appendDeviceLogs(device, [
    `[cloud] ${nowIso()} ignored duplicate firmware report device=${deviceId} ` +
    `version=${reportedVersion} active=${activeVersion}`
  ]);
  broadcast(device);
}

function send(res, status, body, type = "application/json") {
  const payload =
    typeof body === "string" || Buffer.isBuffer(body) ? body : JSON.stringify(body);
  res.writeHead(status, {
    "Content-Type": type,
    "Cache-Control": "no-store",
    "Pragma": "no-cache",
    "Expires": "0",
    "Surrogate-Control": "no-store",
    "Access-Control-Allow-Origin": "*",
  });
  res.end(payload);
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let body = "";
    req.on("data", (chunk) => { body += chunk; });
    req.on("end", () => {
      try { resolve(JSON.parse(body)); }
      catch (e) { reject(e); }
    });
    req.on("error", reject);
  });
}

const firmwareRoutes = createFirmwareRoutes({
  DEVICE_ONLINE_TTL_MS,
  broadcast,
  currentFirmwareVersion,
  firmwareDir: FIRMWARE_DIR,
  getDevice,
  nowIso,
  readBody,
  readFirmwareManifest,
  send,
  shouldIgnoreDuplicateFirmwareReport,
  validDeviceToken,
  validOperator,
});

const deviceRoutes = createDeviceRoutes({
  COMMAND_TTL_MS,
  SSE_HEARTBEAT_MS,
  appendDeviceLogs,
  broadcast,
  clients,
  currentFirmwareVersion,
  getDevice,
  nextStop,
  noteIgnoredDuplicateFirmwareReport,
  readBody,
  reportedFirmwareVersion,
  send,
  shouldIgnoreDuplicateFirmwareReport,
  validDeviceToken,
  validOperator,
  validOperatorQuery,
});

const healthRoutes = createHealthRoutes({
  healthSummary,
  operatorToken: OPERATOR_TOKEN,
  send,
});

const server = http.createServer(async (req, res) => {
  if (req.method === "OPTIONS") {
    send(res, 204, "");
    return;
  }

  const url = new URL(req.url, `http://${req.headers.host}`);

  try {
    if (healthRoutes.handle(req, res, url)) {
      return;
    }

    if (await firmwareRoutes.handle(req, res, url)) {
      return;
    }

    if (await deviceRoutes.handle(req, res, url)) {
      return;
    }

    serveStatic(req, res);
  } catch (err) {
    send(res, 500, { error: err.message });
  }
});

if (require.main === module) {
  if (ALLOW_INSECURE_DEV_AUTH) {
    console.warn("FOLLOWBOX_ALLOW_INSECURE_DEV_AUTH=1: auth disabled for development only");
  } else if (!DEVICE_TOKEN || !OPERATOR_TOKEN) {
    console.warn("FollowBox cloud auth is not fully configured; protected APIs will reject requests");
  }
  server.listen(PORT, "0.0.0.0", () => {
    console.log(`FollowBox cloud server listening on http://0.0.0.0:${PORT}`);
  });
}

module.exports = {
  _test: {
    appendDeviceLogs,
    buildBroadcastPayload,
    MAX_BROADCAST_LOG_LINES,
    MAX_DEVICE_LOG_LINES,
    MAX_LOG_LINE_CHARS,
  },
};
