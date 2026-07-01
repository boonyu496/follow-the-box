const crypto = require("crypto");
const fs = require("fs");
const path = require("path");

function createFirmwareManifestStore(options) {
  const firmwareDir = options.firmwareDir;
  const deviceToken = options.deviceToken || "";
  const manifestPath = path.join(firmwareDir, "manifest.json");

  function exists() {
    return fs.existsSync(manifestPath);
  }

  function read(deviceId) {
    if (!exists()) return null;
    let manifest;
    try {
      const raw = fs.readFileSync(manifestPath, "utf8").replace(/^\uFEFF/, "");
      manifest = JSON.parse(raw);
    } catch {
      return null;
    }
    const file = manifest.file || "firmware.bin";
    const full = path.normalize(path.join(firmwareDir, file));
    if (!full.startsWith(firmwareDir) || !fs.existsSync(full)) return null;
    const stat = fs.statSync(full);
    const actualMd5 = crypto.createHash("md5").update(fs.readFileSync(full)).digest("hex");
    const declaredMd5 = String(manifest.md5 || "").toLowerCase();
    if (!manifest.version || (declaredMd5 && declaredMd5 !== actualMd5)) return null;
    return {
      ok: true,
      version: String(manifest.version || ""),
      url: manifest.url ||
        `/api/device/${encodeURIComponent(deviceId)}/firmware/download?token=${encodeURIComponent(deviceToken)}&version=${encodeURIComponent(manifest.version)}`,
      md5: actualMd5,
      size: stat.size,
      force: !!manifest.force,
      notes: String(manifest.notes || ""),
      file,
    };
  }

  return {
    exists,
    read,
  };
}

module.exports = {
  createFirmwareManifestStore,
};
