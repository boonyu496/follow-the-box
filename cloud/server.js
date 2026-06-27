const http = require("http");
const fs = require("fs");
const path = require("path");
const crypto = require("crypto");

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
const DEVICE_ONLINE_TTL_MS = 10000;
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
    commandAt: device.commandAt,
    ota: device.ota,
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

function healthSummary() {
  return {
    ok: true,
    service: "followbox-cloud",
    at: nowIso(),
    version: deployVersion(),
    firmware_manifest: fs.existsSync(FIRMWARE_MANIFEST),
  };
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
  let manifest;
  try {
    const raw = fs.readFileSync(FIRMWARE_MANIFEST, "utf8").replace(/^\uFEFF/, "");
    manifest = JSON.parse(raw);
  } catch {
    return null;
  }
  const file = manifest.file || "firmware.bin";
  const full = path.normalize(path.join(FIRMWARE_DIR, file));
  if (!full.startsWith(FIRMWARE_DIR) || !fs.existsSync(full)) return null;
  const stat = fs.statSync(full);
  const actualMd5 = crypto.createHash("md5").update(fs.readFileSync(full)).digest("hex");
  const declaredMd5 = String(manifest.md5 || "").toLowerCase();
  if (!manifest.version || (declaredMd5 && declaredMd5 !== actualMd5)) return null;
  return {
    ok: true,
    version: String(manifest.version || ""),
    url: manifest.url ||
      `/api/device/${encodeURIComponent(deviceId)}/firmware/download?token=${encodeURIComponent(DEVICE_TOKEN)}&version=${encodeURIComponent(manifest.version)}`,
    md5: actualMd5,
    size: stat.size,
    force: !!manifest.force,
    notes: String(manifest.notes || ""),
    file,
  };
}

function currentFirmwareVersion(device) {
  return String(device.state?.firmware?.version || device.firmwareVersion || "");
}

function firmwareSummary(deviceId, device, includeDownload, currentOverride = "") {
  const manifest = readFirmwareManifest(deviceId);
  if (!manifest) return null;
  const current = String(currentOverride || currentFirmwareVersion(device));
  // Firmware labels include feature tracks such as tof-debug and lidar-s2 that
  // are not globally ordered. The published manifest is the operator-approved
  // candidate, so any different device version is installable after confirmation.
  const updateAvailable = !!current && manifest.version !== current;
  const summary = {
    ok: true,
    version: manifest.version,
    available_version: manifest.version,
    current_version: current,
    update_available: updateAvailable,
    size: manifest.size,
    md5: manifest.md5,
    force: manifest.force,
    notes: manifest.notes,
    ota: device.ota,
  };
  if (includeDownload) summary.url = manifest.url;
  return summary;
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
  if (url.searchParams.get("version") !== manifest.version) {
    send(res, 409, { ok: false, reason: "firmware version changed" });
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
    /^\/api\/device\/([^/]+)\/firmware\/(version|download|request|install)$/
  );
  const otaResultMatch = url.pathname.match(/^\/api\/device\/([^/]+)\/ota-result$/);
  const videoMatch = url.pathname.match(
    /^\/api\/device\/([^/]+)\/video\/(upload|latest\.jpg|stream)$/
  );
  const match = url.pathname.match(/^\/api\/device\/([^/]+)\/(ingest|command|events)$/);

  try {
    if (url.pathname === "/api/health" && req.method === "GET") {
      send(res, 200, healthSummary());
      return;
    }

    // ── Local config: exposes operator token only to RFC-1918 / loopback clients ──
    if (url.pathname === "/api/config" && req.method === "GET") {
      const remoteIp = req.socket?.remoteAddress || "";
      const isLocal = /^(127\.|::1$|::ffff:127\.|::ffff:10\.|10\.|172\.(1[6-9]|2\d|3[01])\.|192\.168\.|::ffff:192\.168\.)/.test(remoteIp);
      send(res, 200, {
        ok: true,
        local: isLocal,
        operator_token: isLocal ? OPERATOR_TOKEN : "",
      });
      return;
    }

    if (firmwareMatch) {
      const [, deviceId, action] = firmwareMatch;
      const device = getDevice(deviceId);
      if (action === "version" && req.method === "GET") {
        // The device polls with its shared device token. The H5 operator panel
        // checks the same read-only manifest with its Bearer operator token.
        // Keep the credentials separate instead of exposing the device token
        // to browser storage.
        if (!validDeviceToken(null, url) && !validOperator(req)) {
          send(res, 401, { ok: false, reason: "bad token" });
          return;
        }
        const queryToken = url.searchParams.get("token") || "";
        const deviceAuthenticated = DEVICE_TOKEN
          ? queryToken === DEVICE_TOKEN
          : url.searchParams.has("current");
        const reportedCurrent = String(url.searchParams.get("current") || "").slice(0, 64);
        if (deviceAuthenticated && reportedCurrent) {
          device.firmwareVersion = reportedCurrent;
        }
        const summary = firmwareSummary(deviceId, device, false, reportedCurrent);
        if (summary && deviceAuthenticated) {
          const authorized = device.ota.status === "pending" &&
            device.ota.requestedVersion === summary.available_version;
          summary.install_requested = authorized;
          summary.request_id = authorized ? device.ota.requestId : "";
          // Compatibility gate for the pre-consent firmware: its legacy poller
          // only installs when a URL is present. Never reveal that URL until an
          // operator has explicitly created a matching install request.
          if (authorized) {
            summary.url = readFirmwareManifest(deviceId).url;
          }
        }
        send(res, summary ? 200 : 404,
             summary || { ok: false, reason: "firmware not published" });
        return;
      }
      if (action === "request" && req.method === "GET") {
        if (!validDeviceToken(null, url)) {
          send(res, 401, { ok: false, reason: "bad device token" });
          return;
        }
        const current = String(url.searchParams.get("current") || "").slice(0, 64);
        if (current) device.firmwareVersion = current;
        const summary = firmwareSummary(deviceId, device, true, current);
        if (!summary) {
          send(res, 404, { ok: false, reason: "firmware not published" });
          return;
        }
        summary.request_id = device.ota.requestId;
        summary.install_requested =
          device.ota.status === "pending" &&
          device.ota.requestedVersion === summary.available_version;
        send(res, 200, summary);
        return;
      }
      if (action === "install" && req.method === "POST") {
        if (!validOperator(req)) {
          send(res, 401, { ok: false, reason: "bad operator token" });
          return;
        }
        const body = await readBody(req);
        const summary = firmwareSummary(deviceId, device, false);
        const requestedVersion = String(body.version || "");
        const online = device.lastIngestAt > 0 &&
          Date.now() - device.lastIngestAt < DEVICE_ONLINE_TTL_MS;
        if (!summary || requestedVersion !== summary.available_version) {
          send(res, 409, { ok: false, reason: "published version changed" });
          return;
        }
        if (!online || !summary.current_version) {
          send(res, 409, { ok: false, reason: "device offline or current version unknown" });
          return;
        }
        if (!summary.update_available) {
          send(res, 409, { ok: false, reason: "requested version is not newer" });
          return;
        }
        device.ota = {
          requestId: crypto.randomUUID(),
          requestedVersion,
          status: "pending",
          requestedAt: Date.now(),
          updatedAt: Date.now(),
          reason: "",
        };
        device.logs.push(`[ota] ${nowIso()} install requested version=${requestedVersion}`);
        device.logs = device.logs.slice(-500);
        broadcast(device);
        send(res, 202, { ok: true, ota: device.ota });
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
      if (body.request_id && body.request_id === device.ota.requestId) {
        device.ota.status = body.ok ? "restarting" : "failed";
        device.ota.updatedAt = Date.now();
        device.ota.reason = String(body.reason || "");
      }
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
      const reportedVersion = currentFirmwareVersion(device);
      if (reportedVersion && reportedVersion === device.ota.requestedVersion &&
          (device.ota.status === "pending" || device.ota.status === "restarting")) {
        device.ota.status = "installed";
        device.ota.updatedAt = Date.now();
        device.ota.reason = "device confirmed new version";
      }
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
