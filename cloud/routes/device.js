function createDeviceRoutes(options) {
  const {
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
  } = options;

  const videoClients = new Set();

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

  async function handleVideo(req, res, url, deviceId, action) {
    const device = getDevice(deviceId);

    if (action === "upload" && req.method === "POST") {
      if (!validDeviceToken(null, url)) {
        send(res, 401, { ok: false, reason: "bad device token" });
        return true;
      }
      const frame = await readBinaryBody(req, 256 * 1024);
      if (frame.length < 16) {
        send(res, 400, { ok: false, reason: "empty frame" });
        return true;
      }
      device.video.frame = frame;
      device.video.frameSeq += 1;
      device.video.lastFrameAt = Date.now();
      device.video.contentType = req.headers["content-type"] || "image/jpeg";
      broadcastVideo(device);
      broadcast(device);
      send(res, 200, { ok: true, frameSeq: device.video.frameSeq });
      return true;
    }

    if (!validOperatorQuery(url)) {
      send(res, 401, { ok: false, reason: "bad operator token" });
      return true;
    }

    if (action === "latest.jpg" && req.method === "GET") {
      if (!device.video.frame) {
        send(res, 404, "no frame", "text/plain");
        return true;
      }
      send(res, 200, device.video.frame, device.video.contentType);
      return true;
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
      return true;
    }

    return false;
  }

  async function handleDevice(req, res, url, deviceId, action) {
    const device = getDevice(deviceId);

    if (action === "events" && req.method === "GET") {
      // EventSource cannot set headers, so the operator token is accepted via
      // ?token= for this endpoint. Telemetry/logs must not be world-readable.
      if (!validOperatorQuery(url)) {
        send(res, 401, { ok: false, reason: "bad operator token" });
        return true;
      }
      res.writeHead(200, {
        "Content-Type": "text/event-stream",
        "Cache-Control": "no-store",
        "Connection": "keep-alive",
        "Access-Control-Allow-Origin": "*",
      });
      const heartbeat = setInterval(() => {
        res.write(`: heartbeat ${Date.now()}\n\n`);
      }, SSE_HEARTBEAT_MS);
      const client = { res, deviceId };
      req.on("close", () => {
        clearInterval(heartbeat);
        clients.delete(client);
      });
      clients.add(client);
      broadcast(device);
      return true;
    }

    if (action === "ingest" && req.method === "POST") {
      const body = await readBody(req);
      if (!validDeviceToken(body, url)) {
        send(res, 401, { ok: false, reason: "bad device token" });
        return true;
      }
      const incomingState = body.state || null;
      const incomingVersion = reportedFirmwareVersion(incomingState);
      if (shouldIgnoreDuplicateFirmwareReport(deviceId, device, incomingVersion)) {
        noteIgnoredDuplicateFirmwareReport(deviceId, device, incomingVersion);
        send(res, 200, { ok: true, ignored: true, reason: "duplicate firmware report" });
        return true;
      }
      device.lastIngestAt = Date.now();
      device.state = incomingState;
      const reportedVersion = currentFirmwareVersion(device);
      if (reportedVersion && reportedVersion === device.ota.requestedVersion &&
          (device.ota.status === "pending" || device.ota.status === "restarting")) {
        device.ota.status = "installed";
        device.ota.updatedAt = Date.now();
        device.ota.reason = "device confirmed new version";
      }
      appendDeviceLogs(device, body.logs);
      broadcast(device);
      send(res, 200, { ok: true });
      return true;
    }

    if (action === "command" && req.method === "GET") {
      if (!validDeviceToken(null, url)) {
        send(res, 401, { ok: false, reason: "bad device token" });
        return true;
      }
      if (device.command.deadman && Date.now() - device.commandAt > COMMAND_TTL_MS) {
        nextStop(device);
      }
      send(res, 200, { ok: true, ...device.command });
      return true;
    }

    if (action === "command" && req.method === "POST") {
      if (!validOperator(req)) {
        send(res, 401, { ok: false, reason: "bad operator token" });
        return true;
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
      return true;
    }

    return false;
  }

  async function handle(req, res, url) {
    const videoMatch = url.pathname.match(
      /^\/api\/device\/([^/]+)\/video\/(upload|latest\.jpg|stream)$/
    );
    if (videoMatch) {
      const [, deviceId, action] = videoMatch;
      return handleVideo(req, res, url, deviceId, action);
    }

    const match = url.pathname.match(/^\/api\/device\/([^/]+)\/(ingest|command|events)$/);
    if (match) {
      const [, deviceId, action] = match;
      return handleDevice(req, res, url, deviceId, action);
    }

    return false;
  }

  return {
    handle,
  };
}

module.exports = {
  createDeviceRoutes,
};
