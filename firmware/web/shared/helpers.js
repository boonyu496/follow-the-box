// ═══════════════════════════════════════════════════
// FollowBox H5 Shared Helpers
// Shared between firmware/web/ and firmware/data/
// ═══════════════════════════════════════════════════

// ── Mode / Stop labels ──
const modeLabels = {
  BOOT_SELF_TEST: "启动自检",
  SAFE_IDLE: "安全停",
  MANUAL_RC: "遥控手动",
  MANUAL_H5_LOW_SPEED: "H5 手动",
  MANUAL_CLOUD_LOW_SPEED: "云端低速",
  AUTO_FOLLOW: "自动跟随",
  FAULT_LOCKOUT: "故障锁定",
  ESTOP_ACTIVE: "急停触发",
};

const stopLabels = {
  NONE: "无停车原因",
  ESTOP: "急停",
  RC_LOST: "遥控丢失",
  H5_LOST: "H5 断线",
  CLOUD_LOST: "云端断线",
  UWB_LOST: "UWB 丢失",
  OBSTACLE_STOP: "障碍停车",
  LOW_BATTERY: "低电压",
  SENSOR_TIMEOUT: "传感器超时",
  MOTOR_FAULT: "电机故障",
  INSTALL_WIZARD_NOT_DONE: "安装向导未完成",
  WATCHDOG_TIMEOUT: "看门狗超时",
};

// ── Formatting helpers ──
function fmt(value, suffix = "", digits = 0) {
  return typeof value === "number" && Number.isFinite(value)
    ? `${value.toFixed(digits)}${suffix}`
    : "--";
}

function fmtMm(value) {
  return typeof value === "number" && value > 0 ? `${Math.round(value)}mm` : "--";
}

function fmtAge(now, last) {
  if (typeof now !== "number" || typeof last !== "number" || last <= 0) return "--";
  return `${Math.max(0, Math.round((now - last) / 1000))}s 前`;
}

function estimateBatteryPercent(voltage) {
  if (typeof voltage !== "number" || !Number.isFinite(voltage)) return 0;
  return Math.max(0, Math.min(100, ((voltage - 30) / (42 - 30)) * 100));
}

function batteryVoltageSupported(voltage) {
  return typeof voltage === "number" && Number.isFinite(voltage) &&
    voltage > 0 && voltage <= 62;
}

function batteryDisplayText(voltage) {
  if (typeof voltage !== "number" || !Number.isFinite(voltage)) return "--";
  if (!batteryVoltageSupported(voltage)) return `${voltage.toFixed(1)}V 异常`;
  return `${voltage.toFixed(1)}V ${Math.round(estimateBatteryPercent(voltage))}%`;
}

// ── UI state helpers ──
function setTextState(el, ok, warn = false) {
  if (!el) return;
  el.classList.toggle("ok-text", !!ok);
  el.classList.toggle("warn-text", !ok && !!warn);
  el.classList.toggle("danger-text", !ok && !warn);
}

function setDot(el, ok, warn = false) {
  if (!el) return;
  el.classList.toggle("ok", !!ok);
  el.classList.toggle("warn", !ok && !!warn);
}

// ── API helper (no auth — firmware/data uses plain POST) ──
async function postJson(path, body) {
  try {
    await fetch(path, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    });
  } catch (e) {
    /* offline; telemetry badge already shows disconnect */
  }
}
