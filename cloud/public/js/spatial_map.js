// Spatial map canvas renderer for the FollowBox cloud console.
(function initFollowBoxCloudSpatial(global) {
  const {
    MAP_MAX_MM,
    channelValid,
    positiveNumber,
  } = global.FollowBoxCloudShared || {};

  if (!global.FollowBoxCloudShared) {
    throw new Error("FollowBoxCloudShared failed to load before spatial_map.js");
  }

  function distColor(mm) {
    if (mm == null || mm <= 0) return "rgba(128,128,128,0.25)";
    if (mm < 500) return "#dc2626";
    if (mm < 1000) return "#dd5b00";
    return "#1aae39";
  }

  function distGlow(mm, rgb) {
    if (mm == null || mm <= 0) return "rgba(128,128,128,0)";
    return `rgba(${rgb},0.2)`;
  }

  function setupCanvasDPI(canvas) {
    if (!canvas) return;
    const dpr = Math.min(global.devicePixelRatio || 1, 2);
    const rect = canvas.getBoundingClientRect();
    if (rect.width === 0) return;
    const w = Math.round(rect.width * dpr);
    const h = Math.round(rect.height * dpr);
    if (canvas.width === w && canvas.height === h) return;
    canvas.width = w;
    canvas.height = h;
    const ctx = canvas.getContext("2d");
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  }

  function drawSpatialMap(canvas, telemetry) {
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    const W = canvas.clientWidth || canvas.width;
    const H = canvas.clientHeight || canvas.height;
    if (W === 0) return;
    const cx = W / 2, cy = H * 0.52;
    const maxPx = Math.min(cx, cy) * 0.88;
    const scale = maxPx / MAP_MAX_MM;

    ctx.clearRect(0, 0, W, H);
    ctx.fillStyle = "#0a0e18";
    ctx.fillRect(0, 0, W, H);

    ctx.strokeStyle = "rgba(255,255,255,0.05)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(cx, 0); ctx.lineTo(cx, H);
    ctx.moveTo(0, cy); ctx.lineTo(W, cy);
    ctx.stroke();

    const rings = [
      { mm: 500, label: "0.5m", alpha: 0.35 },
      { mm: 1000, label: "1m", alpha: 0.25 },
      { mm: 2000, label: "2m", alpha: 0.15 },
      { mm: 3000, label: "3m", alpha: 0.10 },
    ];
    rings.forEach((r) => {
      const px = r.mm * scale;
      ctx.strokeStyle = `rgba(255,255,255,${r.alpha})`;
      ctx.lineWidth = r.mm <= 1000 ? 1.5 : 0.5;
      ctx.setLineDash(r.mm <= 1000 ? [] : [4, 6]);
      ctx.beginPath();
      ctx.arc(cx, cy, px, 0, Math.PI * 2);
      ctx.stroke();
      ctx.setLineDash([]);
      ctx.fillStyle = "rgba(255,255,255,0.5)";
      ctx.font = "10px system-ui, sans-serif";
      ctx.textAlign = "left";
      ctx.textBaseline = "top";
      ctx.fillText(r.label, cx + px + 4, cy - 4);
    });

    ctx.fillStyle = "rgba(255,255,255,0.35)";
    ctx.font = "11px system-ui, sans-serif";
    ctx.textAlign = "center";
    ctx.textBaseline = "middle";
    ctx.fillText("前", cx, cy - maxPx - 14);
    ctx.fillText("后", cx, cy + maxPx + 14);
    ctx.fillText("左", cx - maxPx - 14, cy);
    ctx.fillText("右", cx + maxPx + 14, cy);

    ctx.strokeStyle = "rgba(0,117,222,0.08)";
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.arc(cx, cy, maxPx, -Math.PI * 0.35, Math.PI * 0.35);
    ctx.stroke();

    const vSize = 20;
    ctx.fillStyle = "#d7dde8";
    ctx.beginPath();
    ctx.moveTo(cx, cy - vSize);
    ctx.lineTo(cx - vSize * 0.6, cy + vSize * 0.5);
    ctx.lineTo(cx, cy + vSize * 0.15);
    ctx.lineTo(cx + vSize * 0.6, cy + vSize * 0.5);
    ctx.closePath();
    ctx.fill();
    ctx.fillStyle = "rgba(255,255,255,0.5)";
    ctx.beginPath();
    ctx.arc(cx, cy, 3, 0, Math.PI * 2);
    ctx.fill();

    const s = telemetry?.state || {};
    const uwb = s.uwb || {};
    const tof = s.tof || {};
    const ultrasonic = s.ultrasonic || {};
    const obstacle = s.obstacle || {};

    function plotSensor(angleDeg, distance_mm, label, sensorType) {
      if (distance_mm == null || distance_mm <= 0) return;
      const distPx = Math.min(distance_mm, MAP_MAX_MM) * scale;
      const a = (-90 - angleDeg) * Math.PI / 180;
      const x = cx + Math.cos(a) * distPx;
      const y = cy + Math.sin(a) * distPx;
      const color = distColor(distance_mm);
      const r = sensorType === "uwb" ? 7 : 5;

      const rgb = distance_mm < 500 ? "220,38,38" : distance_mm < 1000 ? "221,91,0" : "26,174,57";
      const glow = ctx.createRadialGradient(x, y, 0, x, y, r * 3);
      glow.addColorStop(0, distGlow(distance_mm, rgb));
      glow.addColorStop(1, "rgba(0,0,0,0)");
      ctx.fillStyle = glow;
      ctx.beginPath();
      ctx.arc(x, y, r * 3, 0, Math.PI * 2);
      ctx.fill();

      ctx.fillStyle = color;
      ctx.beginPath();
      ctx.arc(x, y, r, 0, Math.PI * 2);
      ctx.fill();

      if (sensorType === "uwb") {
        ctx.strokeStyle = "rgba(255,255,255,0.6)";
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.arc(x, y, r, 0, Math.PI * 2);
        ctx.stroke();
      }

      ctx.fillStyle = "#d7dde8";
      ctx.font = sensorType === "uwb" ? "bold 12px system-ui" : "10px system-ui";
      ctx.textAlign = "center";
      ctx.textBaseline = "bottom";
      const lx = x + (sensorType === "uwb" ? 14 : 0);
      const ly = y - r - 4;
      ctx.fillText(label, lx, ly);

      ctx.fillStyle = "rgba(255,255,255,0.5)";
      ctx.font = "9px system-ui";
      ctx.textBaseline = "top";
      if (sensorType === "uwb") {
        ctx.fillText(`${distance_mm}mm`, lx, ly + 12);
      } else {
        ctx.fillText(`${Math.round(distance_mm / 10) / 100}m`, lx, ly + 12);
      }
    }

    if (channelValid(tof, "front_left_valid", "front_left_mm") ||
        channelValid(tof, "front_center_valid", "front_center_mm") ||
        channelValid(tof, "front_right_valid", "front_right_mm")) {
      plotSensor(-35, tof.front_left_mm, "TOF左", "tof");
      plotSensor(0, tof.front_center_mm, "TOF中", "tof");
      plotSensor(35, tof.front_right_mm, "TOF右", "tof");
    }

    if (obstacle.valid || positiveNumber(obstacle.front_left_mm) ||
        positiveNumber(obstacle.front_center_mm) ||
        positiveNumber(obstacle.front_right_mm)) {
      plotSensor(-30, obstacle.front_left_mm, "障左", "obstacle");
      plotSensor(0, obstacle.front_center_mm, "障前", "obstacle");
      plotSensor(30, obstacle.front_right_mm, "障右", "obstacle");
    }

    if (channelValid(ultrasonic, "left_valid", "left_mm") ||
        channelValid(ultrasonic, "right_valid", "right_mm")) {
      plotSensor(-90, ultrasonic.left_mm, "超声左", "ultra");
      plotSensor(90, ultrasonic.right_mm, "超声右", "ultra");
    }

    if (uwb.valid && uwb.distance_mm > 0) {
      const bearing = Math.max(-85, Math.min(85, uwb.bearing_deg || 0));
      plotSensor(bearing, uwb.distance_mm, "目标", "uwb");
    }

    ctx.fillStyle = "rgba(255,255,255,0.2)";
    ctx.font = "9px system-ui";
    ctx.textAlign = "right";
    ctx.textBaseline = "bottom";
    ctx.fillText(`范围 ${MAP_MAX_MM / 1000}m`, W - 8, H - 8);
  }

  global.FollowBoxCloudSpatial = Object.freeze({
    drawSpatialMap,
    setupCanvasDPI,
  });
})(window);
