const t = {
  pageTitle: "\u672c\u5730\u63a7\u5236\u53f0",
  heroTitle: "FollowBox \u672c\u5730\u63a7\u5236\u53f0",
  heroDesc: "\u4f60\u53ea\u9700\u8981\u70b9\u6309\u94ae\uff0c\u5c31\u80fd\u5b8c\u6210\u5e38\u7528\u7684 Git \u63a8\u9001\u3001boonai \u4e91\u7aef\u90e8\u7f72\u3001\u4e91\u7aef OTA \u53d1\u5e03\uff0c\u4ee5\u53ca\u5bf9 ESP32 \u5f00\u53d1\u677f\u7684\u4e32\u53e3\u5237\u673a\u6216\u5c40\u57df\u7f51 OTA \u4e0a\u4f20\u3002",
  labelServer: "\u670d\u52a1",
  labelRepo: "\u4ed3\u5e93",
  labelBranch: "\u5206\u652f",
  serverRunning: "\u8fd0\u884c\u4e2d",
  stateDirty: "\u6709\u6539\u52a8",
  stateClean: "\u5e72\u51c0",
  quickTitle: "\u4e00\u952e\u64cd\u4f5c",
  quickHint: "\u9ed8\u8ba4\u76ee\u6807\u5df2\u7ecf\u5199\u597d\uff1a\u4ed3\u5e93\u3001\u670d\u52a1\u5668\u3001PEM\u3001\u8fdc\u7aef\u76ee\u5f55\u3001\u4e91\u7aef\u5730\u5740\u3001\u9ed8\u8ba4\u4e32\u53e3\u548c\u9ed8\u8ba4 OTA \u5730\u5740\u90fd\u81ea\u52a8\u9884\u586b\u3002",
  healthTitle: "\u73af\u5883\u68c0\u67e5",
  healthHint: "\u7ea2\u8272\u9879\u8868\u793a\u8fd8\u5dee\u524d\u7f6e\u6761\u4ef6\u3002\u5927\u591a\u6570\u65f6\u5019\u53ea\u9700\u8981\u786e\u8ba4 pio \u53ef\u7528\u3001\u4e32\u53e3\u6b63\u786e\u3001\u5f00\u53d1\u677f\u5728\u7ebf\u3002",
  advancedSummary: "\u9ad8\u7ea7\u5165\u53e3",
  commitMessageLabel: "\u63d0\u4ea4\u8bf4\u660e",
  commitMessagePlaceholder: "\u7559\u7a7a\u5219\u81ea\u52a8\u4f7f\u7528\u9ed8\u8ba4\u63d0\u4ea4\u8bf4\u660e",
  otaBuildTitle: "OTA \u6784\u5efa",
  otaVersionLabel: "\u7248\u672c\u53f7",
  otaBuildHint: "\u6784\u5efa\u540e\u4f1a\u66f4\u65b0\u672c\u5730 cloud/firmware/firmware.bin \u548c manifest.json\u3002",
  defaultConfigSummary: "\u9ed8\u8ba4\u914d\u7f6e",
  localConfigTitle: "\u672c\u673a",
  cloudConfigTitle: "\u4e91\u7aef",
  repoPathLabel: "\u4ed3\u5e93\u8def\u5f84",
  gitUserNameLabel: "Git \u7528\u6237\u540d",
  gitUserEmailLabel: "Git \u90ae\u7bb1",
  pioCommandLabel: "PlatformIO \u547d\u4ee4",
  firmwareEnvLabel: "\u56fa\u4ef6\u73af\u5883\u540d",
  otaEnvLabel: "OTA \u73af\u5883\u540d",
  serialPortLabel: "\u4e32\u53e3\u53f7",
  otaUploadPortLabel: "\u5c40\u57df\u7f51 OTA \u5730\u5740",
  cloudPathLabel: "cloud \u76ee\u5f55",
  cloudPemPathLabel: "PEM \u8def\u5f84",
  cloudRemoteDirLabel: "\u8fdc\u7aef\u76ee\u5f55",
  cloudVerifyUrlLabel: "\u9a8c\u8bc1\u5730\u5740",
  logTitle: "\u8fd0\u884c\u65e5\u5fd7",
  clearLog: "\u6e05\u7a7a\u65e5\u5fd7",
  saveConfig: "\u4fdd\u5b58\u9ed8\u8ba4\u914d\u7f6e",
  pushRepo: "\u4e00\u952e\u63a8\u9001\u5230\u4ed3\u5e93",
  pullRepo: "\u4ece\u4ed3\u5e93\u4e0b\u62c9\u5230\u672c\u5730",
  deployCloudAll: "\u4e00\u952e\u63a8\u9001\u5e76\u90e8\u7f72\u4e91\u7aef",
  deployCloud: "\u4ec5\u90e8\u7f72\u4e91\u7aef",
  publishCloudOta: "\u4e00\u952e\u53d1\u5e03\u4e91\u7aef OTA",
  uploadSerial: "\u4e32\u53e3\u5237\u673a\u5230\u5f00\u53d1\u677f",
  uploadNetwork: "\u5c40\u57df\u7f51 OTA \u5230\u5f00\u53d1\u677f",
  refreshStatus: "\u5237\u65b0\u72b6\u6001",
  gitStatus: "\u67e5\u770b\u72b6\u6001",
  gitCommitPush: "\u6dfb\u52a0 + \u63d0\u4ea4 + \u63a8\u9001",
  buildOta: "\u4ec5\u6784\u5efa OTA \u5305",
  initState: "\u521d\u59cb\u5316\u72b6\u6001",
  logStart: "\u5f00\u59cb",
  logDone: "\u5b8c\u6210",
  logFail: "\u5931\u8d25",
  healthRepo: "\u4ed3\u5e93\u76ee\u5f55",
  healthCloud: "cloud \u76ee\u5f55",
  healthFirmware: "firmware \u76ee\u5f55",
  healthPem: "PEM \u5bc6\u94a5",
  healthPio: "PlatformIO \u547d\u4ee4",
  healthGitIdentity: "Git \u63d0\u4ea4\u8eab\u4efd",
  healthSerial: "\u9ed8\u8ba4\u4e32\u53e3",
  healthOtaHost: "\u9ed8\u8ba4 OTA \u5730\u5740",
  healthReady: "\u5c31\u7eea",
  healthMissing: "\u7f3a\u5931",
  gitAddPathspecLabel: "Git pathspec",
  preflightTitle: "\u6267\u884c\u524d\u68c0\u67e5",
  preflightBlocked: "\u68c0\u67e5\u672a\u901a\u8fc7\uff0c\u5df2\u963b\u6b62\u6267\u884c",
  confirmHint: "\u8bf7\u5728\u770b\u5b8c\u5f71\u54cd\u8303\u56f4\u540e\u518d\u786e\u8ba4\u6267\u884c",
  confirmContinue: "\u786e\u5b9a\u7ee7\u7eed\u6267\u884c\u5417\uff1f",
  lastResultIdle: "\u5c1a\u672a\u6267\u884c\u64cd\u4f5c",
  lastResultOk: "\u6210\u529f",
  lastResultFail: "\u5931\u8d25",
  lastResultBlocked: "\u5df2\u963b\u6b62"
};

const textBindings = {
  heroTitle: "heroTitle",
  heroDesc: "heroDesc",
  labelServer: "labelServer",
  labelRepo: "labelRepo",
  labelBranch: "labelBranch",
  serverState: "serverRunning",
  quickTitle: "quickTitle",
  quickHint: "quickHint",
  healthTitle: "healthTitle",
  healthHint: "healthHint",
  advancedSummary: "advancedSummary",
  gitAddPathspecLabel: "gitAddPathspecLabel",
  commitMessageLabel: "commitMessageLabel",
  otaBuildTitle: "otaBuildTitle",
  otaVersionLabel: "otaVersionLabel",
  otaBuildHint: "otaBuildHint",
  defaultConfigSummary: "defaultConfigSummary",
  localConfigTitle: "localConfigTitle",
  cloudConfigTitle: "cloudConfigTitle",
  repoPathLabel: "repoPathLabel",
  gitUserNameLabel: "gitUserNameLabel",
  gitUserEmailLabel: "gitUserEmailLabel",
  pioCommandLabel: "pioCommandLabel",
  firmwareEnvLabel: "firmwareEnvLabel",
  otaEnvLabel: "otaEnvLabel",
  serialPortLabel: "serialPortLabel",
  otaUploadPortLabel: "otaUploadPortLabel",
  cloudPathLabel: "cloudPathLabel",
  cloudPemPathLabel: "cloudPemPathLabel",
  cloudRemoteDirLabel: "cloudRemoteDirLabel",
  cloudVerifyUrlLabel: "cloudVerifyUrlLabel",
  logTitle: "logTitle"
};

const buttonTextBindings = {
  clearLog: "clearLog",
  saveConfig: "saveConfig",
  pushRepo: "pushRepo",
  pullRepo: "pullRepo",
  deployCloudAll: "deployCloudAll",
  deployCloud: "deployCloud",
  publishCloudOta: "publishCloudOta",
  uploadSerial: "uploadSerial",
  uploadNetwork: "uploadNetwork",
  refreshStatus: "refreshStatus",
  gitStatus: "gitStatus",
  gitPull: "pullRepo",
  gitCommitPush: "gitCommitPush",
  buildOta: "buildOta"
};

function setText(id, key) {
  document.getElementById(id).textContent = t[key];
}

function applyTextBindings(bindings) {
  Object.entries(bindings).forEach(([id, key]) => setText(id, key));
}

document.title = `FollowBox ${t.pageTitle}`;
applyTextBindings(textBindings);
applyTextBindings(buttonTextBindings);
document.getElementById("commitMessage").placeholder = t.commitMessagePlaceholder;

const ids = [
  "repoPath","cloudPath","cloudPemPath","cloudHost","cloudPort","cloudUser","cloudRemoteDir","cloudVerifyUrl",
  "gitUserName","gitUserEmail","pioCommand","firmwareEnv","otaEnv","serialPort","otaUploadPort","otaVersion","gitAddPathspec"
];
const els = Object.fromEntries(ids.map((id) => [id, document.getElementById(id)]));
const buttonIds = [
  "refreshStatus","pushRepo","pullRepo","deployCloudAll","deployCloud","publishCloudOta",
  "uploadSerial","uploadNetwork","gitStatus","gitFetch","gitPull","gitCommitPush","buildOta",
  "clearLog","saveConfig"
];
const buttons = Object.fromEntries(buttonIds.map((id) => [id, document.getElementById(id)]));
const logEl = document.getElementById("log");
const repoStateEl = document.getElementById("repoState");
const branchStateEl = document.getElementById("branchState");
const healthEl = document.getElementById("health");
const lastResultEl = document.getElementById("lastResult");
lastResultEl.textContent = t.lastResultIdle;

function appendLog(title, data) {
  const stamp = new Date().toLocaleString();
  const lines = [`[${stamp}] ${title}`];
  if (typeof data === "string") {
    lines.push(data);
  } else if (data) {
    lines.push(JSON.stringify(data, null, 2));
  }
  logEl.textContent += lines.join("\n") + "\n\n";
  logEl.scrollTop = logEl.scrollHeight;
}

function shortText(value, limit = 1200) {
  const text = String(value || "").trim();
  if (text.length <= limit) return text;
  return `${text.slice(0, limit)}\n... (${text.length - limit} more chars)`;
}

function collectCommands(data, prefix = "") {
  const rows = [];
  if (!data || typeof data !== "object") return rows;
  if (typeof data.command === "string" && Object.prototype.hasOwnProperty.call(data, "exitCode")) {
    rows.push({ ...data, label: prefix || "command" });
  }
  for (const [key, value] of Object.entries(data)) {
    if (!value) continue;
    const label = prefix ? `${prefix}.${key}` : key;
    if (Array.isArray(value)) {
      value.forEach((item, index) => rows.push(...collectCommands(item, `${label}[${index}]`)));
    } else if (typeof value === "object") {
      rows.push(...collectCommands(value, label));
    }
  }
  return rows;
}

function summarizeResult(data) {
  const lines = [];
  if (!data || typeof data !== "object") return "";
  if (Object.prototype.hasOwnProperty.call(data, "ok")) {
    lines.push(`结果: ${data.ok ? t.lastResultOk : t.lastResultFail}`);
  }
  if (data.stage) lines.push(`阶段: ${data.stage}`);
  if (data.step) lines.push(`步骤: ${data.step}`);
  if (data.reason) lines.push(`原因: ${data.reason}`);
  if (data.error) lines.push(`错误: ${data.error}`);
  if (data.path) lines.push(`接口: ${data.path}`);
  const commands = collectCommands(data);
  if (commands.length) {
    lines.push("命令:");
    commands.forEach((cmd) => {
      lines.push(`- ${cmd.label}: exit ${cmd.exitCode} | ${cmd.command}`);
      if (cmd.stderr) lines.push(`  stderr: ${shortText(cmd.stderr, 500)}`);
      if (cmd.stdout) lines.push(`  stdout: ${shortText(cmd.stdout, 500)}`);
    });
  }
  if (data.details?.identity && (data.details.identity.effectiveName || data.details.identity.effectiveEmail)) {
    lines.push(`Git: ${data.details.identity.effectiveName || "--"} <${data.details.identity.effectiveEmail || "--"}>`);
  }
  return lines.join("\n");
}

function renderLastResult(title, data, blocked = false) {
  const ok = !!(data && data.ok);
  const state = blocked ? t.lastResultBlocked : (ok ? t.lastResultOk : t.lastResultFail);
  const summary = summarizeResult(data);
  lastResultEl.className = `result ${ok && !blocked ? "ok" : "bad"}`;
  lastResultEl.textContent = `${title}: ${state}${summary ? "\n" + summary : ""}`;
}

function applyResponseState(data) {
  if (!data || typeof data !== "object") return;
  if (data.git) {
    repoStateEl.textContent = isRepoDirty(data.git) ? t.stateDirty : t.stateClean;
    branchStateEl.textContent = data.git.branch || "--";
  }
  if (data.health) {
    renderHealth(data.health);
  }
  if (data.config) {
    applyConfig(data.config);
  }
}

function recordActionResult(title, data) {
  appendLog(`${title} -> ${t.logDone}`, summarizeResult(data));
  appendLog(`${title} -> raw`, data);
  renderLastResult(title, data);
  applyResponseState(data);
}

function isRepoDirty(git) {
  const status = String(git?.status || "").trim();
  if (!status) return false;
  const lines = status.split(/\r?\n/).map((line) => line.trim()).filter(Boolean);
  return lines.some((line) => !line.startsWith("##"));
}

function summarizePreflight(data) {
  const lines = [];
  if (data.summary) lines.push(data.summary);
  if (Array.isArray(data.checks) && data.checks.length) {
    lines.push("检查项:");
    data.checks.forEach((item) => {
      const suffix = item.value ? ` (${item.value})` : "";
      lines.push(`- ${item.ok ? "OK" : "FAIL"} ${item.label}${suffix}`);
    });
  }
  if (Array.isArray(data.warnings) && data.warnings.length) {
    lines.push("提醒:");
    data.warnings.forEach((item) => lines.push(`- ${item}`));
  }
  if (data.details?.git?.files?.length) {
    lines.push("候选文件:");
    data.details.git.files.slice(0, 12).forEach((item) => {
      lines.push(`- ${item.status} ${item.path}`);
    });
    if (data.details.git.files.length > 12) {
      lines.push(`- ... 共 ${data.details.git.files.length} 个文件`);
    }
  }
  if (data.details?.serial?.matched?.name) {
    lines.push(`目标串口: ${data.details.serial.matched.name}`);
  }
  if (data.details?.network?.target) {
    lines.push(`OTA 目标: ${data.details.network.target}`);
  }
  if (data.details?.cloud) {
    lines.push(`云端目标: ${data.details.cloud.host}:${data.details.cloud.port} ${data.details.cloud.remoteDir}`);
  }
  return lines.join("\n");
}

async function runWithPreflight(button, title, action, path, bodyBuilder) {
  try {
    setBusy(button, true);
    const body = bodyBuilder ? bodyBuilder() : {};
    const preflightBody = { ...body, action };
    appendLog(`${title} -> ${t.preflightTitle}`, preflightBody);
    const preflight = await api("/api/preflight", preflightBody);
    appendLog(`${title} -> ${t.preflightTitle}`, preflight);
    if (!preflight.ok) {
      appendLog(`${title} -> ${t.preflightBlocked}`, summarizePreflight(preflight));
      renderLastResult(title, preflight, true);
      return;
    }
    const confirmed = window.confirm(`${summarizePreflight(preflight)}\n\n${t.confirmHint}\n${t.confirmContinue}`);
    if (!confirmed) {
      appendLog(`${title} -> 已取消`, "用户取消执行");
      return;
    }
    appendLog(`${title} -> ${t.logStart}`, body || "");
    const data = await api(path, body);
    recordActionResult(title, data);
    return data;
  } catch (error) {
    appendLog(`${title} -> ${t.logFail}`, error.data || error.message);
    renderLastResult(title, error.data || { ok: false, error: error.message });
  } finally {
    setBusy(button, false);
  }
}

function payload() {
  return {
    ...Object.fromEntries(ids.map((id) => [id, els[id].value])),
    commitMessage: document.getElementById("commitMessage").value
  };
}

function applyConfig(config) {
  ids.forEach((id) => {
    if (config[id] === undefined || config[id] === null) return;
    els[id].value = String(config[id]);
  });
}

function renderHealth(health) {
  const gitIdentityReady = !!(health.gitUserName && health.gitUserEmail);
  const gitIdentityText = gitIdentityReady
    ? `${health.gitUserName} <${health.gitUserEmail}>`
    : t.healthMissing;
  const rows = [
    [t.healthRepo, health.repoExists],
    [t.healthCloud, health.cloudExists],
    [t.healthFirmware, health.firmwareExists],
    [t.healthPem, health.pemExists],
    [t.healthPio, health.pioExists],
    [t.healthGitIdentity, gitIdentityText, gitIdentityReady],
    [t.healthSerial, health.serialPort || "--"],
    [t.healthOtaHost, health.otaUploadPort || "--"]
  ];
  healthEl.innerHTML = rows.map(([label, value, explicitOk]) => {
    const ok = typeof explicitOk === "boolean" ? explicitOk : (typeof value === "boolean" ? value : true);
    const text = typeof value === "boolean" ? (value ? t.healthReady : t.healthMissing) : value;
    return `<div class="health-item"><span>${label}</span><strong class="${ok ? "ok" : "bad"}">${text}</strong></div>`;
  }).join("");
}

async function api(path, body) {
  const resp = await fetch(path, {
    method: body ? "POST" : "GET",
    headers: body ? { "Content-Type": "application/json" } : undefined,
    body: body ? JSON.stringify(body) : undefined
  });
  const data = await resp.json();
  if (!resp.ok) {
    const error = new Error(data.error || `HTTP ${resp.status}`);
    error.data = data;
    throw error;
  }
  return data;
}

function setBusy(button, busy) {
  if (button) button.disabled = busy;
}

async function run(button, title, path, bodyBuilder) {
  try {
    setBusy(button, true);
    const body = bodyBuilder ? bodyBuilder() : null;
    appendLog(`${title} -> ${t.logStart}`, body || "");
    const data = await api(path, body);
    recordActionResult(title, data);
    return data;
  } catch (error) {
    appendLog(`${title} -> ${t.logFail}`, error.data || error.message);
    renderLastResult(title, error.data || { ok: false, error: error.message });
  } finally {
    setBusy(button, false);
  }
}

async function refreshStatus() {
  const data = await api("/api/state");
  applyResponseState(data);
  appendLog(t.initState, data);
}

const actionGroups = {
  gitPreflight: [
    ["pushRepo", t.pushRepo, "git-commit-push", "/api/git/commit-push", payload],
    ["pullRepo", t.pullRepo, "git-pull-local", "/api/git/pull", payload],
    ["gitPull", t.pullRepo, "git-pull-local", "/api/git/pull", payload],
    ["gitCommitPush", t.gitCommitPush, "git-commit-push", "/api/git/commit-push", payload]
  ],
  cloudPreflight: [
    ["deployCloudAll", t.deployCloudAll, "deploy-cloud-all", "/api/cloud/deploy-all", payload],
    ["deployCloud", t.deployCloud, "cloud-deploy", "/api/cloud/deploy", payload]
  ],
  otaPreflight: [
    ["publishCloudOta", t.publishCloudOta, "ota-publish-cloud", "/api/ota/publish-cloud", payload],
    ["uploadSerial", t.uploadSerial, "upload-serial", "/api/ota/upload-serial", payload],
    ["uploadNetwork", t.uploadNetwork, "upload-network", "/api/ota/upload-network", payload]
  ],
  statusDirect: [
    ["refreshStatus", t.refreshStatus, "/api/state"]
  ],
  gitDirect: [
    ["gitStatus", t.gitStatus, "/api/git/status", payload],
    ["gitFetch", "Git Fetch", "/api/git/fetch"]
  ],
  otaDirect: [
    ["buildOta", t.buildOta, "/api/ota/build", payload]
  ],
  configDirect: [
    ["saveConfig", t.saveConfig, "/api/config/save", payload]
  ]
};

function bindPreflightActions(actions) {
  actions.forEach(([id, title, action, path, bodyBuilder]) => {
    buttons[id].onclick = () => runWithPreflight(buttons[id], title, action, path, bodyBuilder);
  });
}

function bindDirectActions(actions) {
  actions.forEach(([id, title, path, bodyBuilder]) => {
    buttons[id].onclick = () => run(buttons[id], title, path, bodyBuilder);
  });
}

function bindActionHandlers() {
  [
    actionGroups.gitPreflight,
    actionGroups.cloudPreflight,
    actionGroups.otaPreflight
  ].forEach(bindPreflightActions);
  [
    actionGroups.statusDirect,
    actionGroups.gitDirect,
    actionGroups.otaDirect,
    actionGroups.configDirect
  ].forEach(bindDirectActions);
  buttons.clearLog.onclick = () => { logEl.textContent = ""; };
}

bindActionHandlers();
refreshStatus();
