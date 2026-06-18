const http = require("http");
const fs = require("fs");
const path = require("path");

const PORT = Number(process.env.PORT || 8080);
const DEVICE_TOKEN =
  process.env.FOLLOWBOX_DEVICE_TOKEN || "f892ef460de624143d7d65cb5a863f84";
const OPERATOR_TOKEN =
  process.env.FOLLOWBOX_OPERATOR_TOKEN || "0b6cf31c57bc202d002b04f843c9b430";
const PUBLIC_DIR = path.join(__dirname, "public");
const DEPLOY_VERSION_FILE = path.join(PUBLIC_DIR, "deploy-version.txt");
const FIRMWARE_DIR = path.join(__dirname, "firmware");
const FIRMWARE_MANIFEST = path.join(FIRMWARE_DIR, "manifest.json");
const COMMAND_TTL_MS = 750;
const DEVICE_ONLINE_TTL_MS = 5000;
const SSE_HEARTBEAT_MS = 15000;

const devices = new Map();
const clients = new Set();
const videoClients = new Set();

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
      state: null,
      logs: [],
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

function validDeviceToken(body, url) {
  const token = body?.token || url.searchParams.get("token") || "";
  return !DEVICE_TOKEN || token === DEVICE_TOKEN;
}

function validOperator(req) {
  if (!OPERATOR_TOKEN) return true;
  const auth = req.headers.authorization || "";
  return auth === `Bearer ${OPERATOR_TOKEN}`;
}

function broadcast(device) {
  const now = Date.now();
  const event = JSON.stringify({
    id: device.id,
    at: nowIso(),
    lastIngestAt: device.lastIngestAt,
    online: device.lastIngestAt > 0 && now - device.lastIngestAt < DEVICE_ONLINE_TTL_MS,
    state: device.state,
    logs: device.logs.slice(-200),
    command: device.command,
    video: {
      lastFrameAt: device.video.lastFrameAt,
      frameSeq: device.video.frameSeq,
      online: Date.now() - device.video.lastFrameAt < 5000,
    },
  });
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

function writeVideoPart(res, frame, contentType) {
  res.write(`--followboxframe\r\n`);
  res.write(`Content-Type: ${contentType || "image/jpeg"}\r\n`);
  res.write(`Content-Length: ${frame.length}\r\n\r\n`);
  res.write(frame);
  res.write("\r\n");
}

function broadcastVideo(device) {
  if (!device.video.frame) return;
  for (const client of videoClients) {
    if (client.deviceId !== device.id) continue;
    try {
      writeVideoPart(client.res, device.video.frame, device.video.contentType);
    } catch (err) {
      videoClients.delete(client);
    }
  }
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

function readFirmwareManifest(deviceId) {
  if (!fs.existsSync(FIRMWARE_MANIFEST)) return null;
  const manifest = JSON.parse(fs.readFileSync(FIRMWARE_MANIFEST, "utf8"));
  const file = manifest.file || "firmware.bin";
  const full = path.normalize(path.join(FIRMWARE_DIR, file));
  if (!full.startsWith(FIRMWARE_DIR) || !fs.existsSync(full)) return null;
  const stat = fs.statSync(full);
  return {
    ok: true,
    version: String(manifest.version || ""),
    url: manifest.url ||
      `/api/device/${encodeURIComponent(deviceId)}/firmware/download?token=${encodeURIComponent(DEVICE_TOKEN)}`,
    md5: String(manifest.md5 || ""),
    size: Number(manifest.size || stat.size),
    force: !!manifest.force,
    file,
  };
}

function serveFirmwareDownload(req, res, deviceId, url) {
  if (!validDeviceToken(null, url)) {
    send(res, 401, { ok: false, reason: "bad device token" });
    return;
  }
  const manifest = readFirmwareManifest(deviceId);
  if (!manifest) {
    send(res, 404, { ok: false, reason: "firmware not published" });
    return;
  }
  const full = path.normalize(path.join(FIRMWARE_DIR, manifest.file));
  res.writeHead(200, {
    "Content-Type": "application/octet-stream",
    "Content-Length": manifest.size,
    "Cache-Control": "no-store",
    "Pragma": "no-cache",
    "Expires": "0",
  });
  fs.createReadStream(full).pipe(res);
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

function readBinaryBody(req, maxBytes) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let total = 0;
    req.on("data", (chunk) => {
      total += chunk.length;
      if (total > maxBytes) {
        reject(new Error("body too large"));
        req.destroy();
        return;
      }
      chunks.push(chunk);
    });
    req.on("end", () => resolve(Buffer.concat(chunks, total)));
    req.on("error", reject);
  });
}

function nextStop(device) {
  device.seq += 1;
  device.command = { seq: device.seq, deadman: false, forward: 0, turn: 0, safe_idle: false };
  device.commandAt = Date.now();
  return device.command;
}

const server = http.createServer(async (req, res) => {
  if (req.method === "OPTIONS") {
    send(res, 204, "");
    return;
  }

  const url = new URL(req.url, `http://${req.headers.host}`);
  const firmwareMatch = url.pathname.match(
    /^\/api\/device\/([^/]+)\/firmware\/(version|download)$/
  );
  const otaResultMatch = url.pathname.match(/^\/api\/device\/([^/]+)\/ota-result$/);
  const videoMatch = url.pathname.match(
    /^\/api\/device\/([^/]+)\/video\/(upload|latest\.jpg|stream)$/
  );
  const match = url.pathname.match(/^\/api\/device\/([^/]+)\/(ingest|command|events)$/);

  try {
    if (firmwareMatch) {
      const [, deviceId, action] = firmwareMatch;
      if (action === "version" && req.method === "GET") {
        if (!validDeviceToken(null, url)) {
          send(res, 401, { ok: false, reason: "bad device token" });
          return;
        }
        const manifest = readFirmwareManifest(deviceId);
        send(res, manifest ? 200 : 404,
             manifest || { ok: false, reason: "firmware not published" });
        return;
      }
      if (action === "download" && req.method === "GET") {
        serveFirmwareDownload(req, res, deviceId, url);
        return;
      }
    }

    if (otaResultMatch && req.method === "POST") {
      const [, deviceId] = otaResultMatch;
      const body = await readBody(req);
      if (!validDeviceToken(body, url)) {
        send(res, 401, { ok: false, reason: "bad device token" });
        return;
      }
      const device = getDevice(deviceId);
      device.logs.push(`[ota] ${nowIso()} version=${body.version || ""} ok=${!!body.ok} reason=${body.reason || ""}`);
      device.logs = device.logs.slice(-500);
      broadcast(device);
      send(res, 200, { ok: true });
      return;
    }

    if (videoMatch) {
      const [, deviceId, action] = videoMatch;
      const device = getDevice(deviceId);

      if (action === "upload" && req.method === "POST") {
        if (!validDeviceToken(null, url)) {
          send(res, 401, { ok: false, reason: "bad device token" });
          return;
        }
        const frame = await readBinaryBody(req, 256 * 1024);
        if (frame.length < 16) {
          send(res, 400, { ok: false, reason: "empty frame" });
          return;
        }
        device.video.frame = frame;
        device.video.frameSeq += 1;
        device.video.lastFrameAt = Date.now();
        device.video.contentType = req.headers["content-type"] || "image/jpeg";
        broadcastVideo(device);
        broadcast(device);
        send(res, 200, { ok: true, frameSeq: device.video.frameSeq });
        return;
      }

      const token = url.searchParams.get("token") || "";
      if (OPERATOR_TOKEN && token !== OPERATOR_TOKEN) {
        send(res, 401, { ok: false, reason: "bad operator token" });
        return;
      }

      if (action === "latest.jpg" && req.method === "GET") {
        if (!device.video.frame) {
          send(res, 404, "no frame", "text/plain");
          return;
        }
        send(res, 200, device.video.frame, device.video.contentType);
        return;
      }

      if (action === "stream" && req.method === "GET") {
        res.writeHead(200, {
          "Content-Type": "multipart/x-mixed-replace; boundary=followboxframe",
          "Cache-Control": "no-store",
          "Connection": "keep-alive",
          "Access-Control-Allow-Origin": "*",
        });
        const client = { res, deviceId };
        videoClients.add(client);
        req.on("close", () => videoClients.delete(client));
        if (device.video.frame) {
          writeVideoPart(res, device.video.frame, device.video.contentType);
        }
        return;
      }
    }

    if (!match) {
      serveStatic(req, res);
      return;
    }

    const [, deviceId, action] = match;
    const device = getDevice(deviceId);

    if (action === "events" && req.method === "GET") {
      // EventSource cannot set headers, so the operator token is accepted via
      // ?token= for this endpoint. Telemetry/logs must not be world-readable.
      const token = url.searchParams.get("token") || "";
      if (OPERATOR_TOKEN && token !== OPERATOR_TOKEN) {
        send(res, 401, { ok: false, reason: "bad operator token" });
        return;
      }
      res.writeHead(200, {
        "Content-Type": "text/event-stream",
        "Cache-Control": "no-store",
        "Connection": "keep-alive",
        "Access-Control-Allow-Origin": "*",
      });
      const client = { res, deviceId };
      clients.add(client);
      const heartbeat = setInterval(() => {
        res.write(`: heartbeat ${Date.now()}\n\n`);
      }, SSE_HEARTBEAT_MS);
      req.on("close", () => {
        clearInterval(heartbeat);
        clients.delete(client);
      });
      broadcast(device);
      return;
    }

    if (action === "ingest" && req.method === "POST") {
      const body = await readBody(req);
      if (!validDeviceToken(body, url)) {
        send(res, 401, { ok: false, reason: "bad device token" });
        return;
      }
      device.lastIngestAt = Date.now();
      device.state = body.state || null;
      if (Array.isArray(body.logs) && body.logs.length) {
        device.logs.push(...body.logs.map((line) => String(line)).slice(-50));
        device.logs = device.logs.slice(-500);
      }
      broadcast(device);
      send(res, 200, { ok: true });
      return;
    }

    if (action === "command" && req.method === "GET") {
      if (!validDeviceToken(null, url)) {
        send(res, 401, { ok: false, reason: "bad device token" });
        return;
      }
      if (device.command.deadman && Date.now() - device.commandAt > COMMAND_TTL_MS) {
        nextStop(device);
      }
      send(res, 200, { ok: true, ...device.command });
      return;
    }

    if (action === "command" && req.method === "POST") {
      if (!validOperator(req)) {
        send(res, 401, { ok: false, reason: "bad operator token" });
        return;
      }
      const body = await readBody(req);
      const forward = Math.max(-1, Math.min(1, Number(body.forward || 0)));
      const turn = Math.max(-1, Math.min(1, Number(body.turn || 0)));
      device.seq += 1;
      device.command = {
        seq: device.seq,
        deadman: !!body.deadman && !body.safe_idle,
        forward,
        turn,
        // safe_idle must reach the firmware (it parses this key); dropping it
        // here previously made the cloud panel's safety stop a no-op.
        safe_idle: !!body.safe_idle,
      };
      device.commandAt = Date.now();
      broadcast(device);
      send(res, 200, { ok: true, ...device.command });
      return;
    }

    serveStatic(req, res);
  } catch (err) {
    send(res, 500, { error: err.message });
  }
});

server.listen(PORT, "0.0.0.0", () => {
  console.log(`FollowBox cloud server listening on http://0.0.0.0:${PORT}`);
});
