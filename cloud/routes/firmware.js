const crypto = require("crypto");
const fs = require("fs");
const path = require("path");

function createFirmwareRoutes(options) {
  const {
    DEVICE_ONLINE_TTL_MS,
    broadcast,
    currentFirmwareVersion,
    firmwareDir,
    getDevice,
    nowIso,
    readBody,
    readFirmwareManifest,
    send,
    shouldIgnoreDuplicateFirmwareReport,
    validDeviceToken,
    validOperator,
  } = options;

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
    const full = path.normalize(path.join(firmwareDir, manifest.file));
    res.writeHead(200, {
      "Content-Type": "application/octet-stream",
      "Content-Length": manifest.size,
      "Cache-Control": "no-store",
      "Pragma": "no-cache",
      "Expires": "0",
    });
    fs.createReadStream(full).pipe(res);
  }

  async function handleFirmware(req, res, url, deviceId, action) {
    const device = getDevice(deviceId);

    if (action === "version" && req.method === "GET") {
      // The device polls with its shared device token. The H5 operator panel
      // checks the same read-only manifest with its Bearer operator token.
      // Keep the credentials separate instead of exposing the device token
      // to browser storage.
      if (!validDeviceToken(null, url) && !validOperator(req)) {
        send(res, 401, { ok: false, reason: "bad token" });
        return true;
      }
      const deviceAuthenticated = validDeviceToken(null, url);
      const reportedCurrent = String(url.searchParams.get("current") || "").slice(0, 64);
      if (deviceAuthenticated && reportedCurrent &&
          !shouldIgnoreDuplicateFirmwareReport(deviceId, device, reportedCurrent)) {
        device.firmwareVersion = reportedCurrent;
      }
      const summary = firmwareSummary(
        deviceId, device, false, deviceAuthenticated ? reportedCurrent : ""
      );
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
      return true;
    }

    if (action === "request" && req.method === "GET") {
      if (!validDeviceToken(null, url)) {
        send(res, 401, { ok: false, reason: "bad device token" });
        return true;
      }
      const current = String(url.searchParams.get("current") || "").slice(0, 64);
      if (current && !shouldIgnoreDuplicateFirmwareReport(deviceId, device, current)) {
        device.firmwareVersion = current;
      }
      const summary = firmwareSummary(deviceId, device, true, current);
      if (!summary) {
        send(res, 404, { ok: false, reason: "firmware not published" });
        return true;
      }
      summary.request_id = device.ota.requestId;
      summary.install_requested =
        device.ota.status === "pending" &&
        device.ota.requestedVersion === summary.available_version;
      send(res, 200, summary);
      return true;
    }

    if (action === "install" && req.method === "POST") {
      if (!validOperator(req)) {
        send(res, 401, { ok: false, reason: "bad operator token" });
        return true;
      }
      const body = await readBody(req);
      const summary = firmwareSummary(deviceId, device, false);
      const requestedVersion = String(body.version || "");
      const online = device.lastIngestAt > 0 &&
        Date.now() - device.lastIngestAt < DEVICE_ONLINE_TTL_MS;
      if (!summary || requestedVersion !== summary.available_version) {
        send(res, 409, { ok: false, reason: "published version changed" });
        return true;
      }
      if (!online || !summary.current_version) {
        send(res, 409, { ok: false, reason: "device offline or current version unknown" });
        return true;
      }
      if (!summary.update_available) {
        send(res, 409, { ok: false, reason: "requested version is not newer" });
        return true;
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
      return true;
    }

    if (action === "download" && req.method === "GET") {
      serveFirmwareDownload(req, res, deviceId, url);
      return true;
    }

    return false;
  }

  async function handleOtaResult(req, res, url, deviceId) {
    if (req.method !== "POST") return false;

    const body = await readBody(req);
    if (!validDeviceToken(body, url)) {
      send(res, 401, { ok: false, reason: "bad device token" });
      return true;
    }
    const device = getDevice(deviceId);
    if (body.request_id && body.request_id === device.ota.requestId) {
      device.ota.status = body.ok ? "restarting" : "failed";
      device.ota.updatedAt = Date.now();
      device.ota.reason = String(body.reason || "");
    }
    device.logs.push(
      `[ota] ${nowIso()} version=${body.version || ""} ok=${!!body.ok} reason=${body.reason || ""}`
    );
    device.logs = device.logs.slice(-500);
    broadcast(device);
    send(res, 200, { ok: true });
    return true;
  }

  async function handle(req, res, url) {
    const firmwareMatch = url.pathname.match(
      /^\/api\/device\/([^/]+)\/firmware\/(version|download|request|install)$/
    );
    if (firmwareMatch) {
      const [, deviceId, action] = firmwareMatch;
      return handleFirmware(req, res, url, deviceId, action);
    }

    const otaResultMatch = url.pathname.match(/^\/api\/device\/([^/]+)\/ota-result$/);
    if (otaResultMatch) {
      const [, deviceId] = otaResultMatch;
      return handleOtaResult(req, res, url, deviceId);
    }

    return false;
  }

  return {
    handle,
  };
}

module.exports = {
  createFirmwareRoutes,
};
