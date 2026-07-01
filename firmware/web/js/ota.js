// FollowBox embedded H5 explicit-consent OTA controls.
// This module only requests existing OTA APIs; device firmware owns all flash writes and safety stops.

let otaAvailableVersion = "";
let otaPollTimer = null;

function renderOtaStatus(st) {
  otaEls.current.textContent = st.current_version || "--";
  otaEls.available.textContent = st.available_version || "--";
  otaAvailableVersion = st.update_available ? (st.available_version || "") : "";
  const busy = st.state === "installing" || st.state === "rebooting";
  otaEls.check.disabled = busy;
  otaEls.install.hidden = false;
  otaEls.install.disabled = busy || !otaAvailableVersion;
  otaEls.install.textContent = busy
    ? (st.state === "rebooting" ? "正在重启" : "正在安装")
    : (otaAvailableVersion ? "安装更新" : "暂无可安装更新");
  otaEls.later.hidden = !otaAvailableVersion || busy;

  const labels = {
    idle: "已是最新",
    checking: "检查中",
    update_available: "发现新版本",
    installing: "正在安装",
    rebooting: "校验完成，正在重启",
    failed: "更新检查/安装失败",
  };
  otaEls.state.textContent = labels[st.state] || "未检查";
  setTextState(otaEls.state, st.state === "idle", st.state === "checking");
  otaEls.hint.textContent = st.state === "failed"
    ? `失败：${st.reason || "未知原因"}。若安装已开始，车辆将保持安全停机，请受控重启或 USB 恢复。`
    : st.update_available
      ? `新版本 ${st.available_version} 可安装；不点击安装不会写入固件。`
      : "检查更新不会自动安装。";
}

async function refreshOtaStatus() {
  try {
    const res = await fetch("/api/ota/status", { cache: "no-store" });
    if (res.ok) renderOtaStatus(await res.json());
  } catch (e) {
    otaEls.state.textContent = "设备连接失败";
  }
}

function pollOtaStatus() {
  clearInterval(otaPollTimer);
  let remaining = 30;
  otaPollTimer = setInterval(async () => {
    await refreshOtaStatus();
    remaining -= 1;
    if (remaining <= 0) clearInterval(otaPollTimer);
  }, 1000);
}

otaEls.check?.addEventListener("click", async () => {
  otaEls.check.disabled = true;
  otaEls.install.disabled = true;
  otaEls.install.textContent = "检查中";
  otaEls.state.textContent = "提交检查请求";
  try {
    const res = await fetch("/api/ota/check", { method: "POST", headers: authHeaders() });
    if (!res.ok) throw new Error(res.status === 401 ? "本地控制 Key 无效" : "设备未联网或 OTA 忙");
    pollOtaStatus();
  } catch (e) {
    otaEls.state.textContent = "检查失败";
    otaEls.hint.textContent = e.message;
  } finally {
    otaEls.check.disabled = false;
  }
});

otaEls.install?.addEventListener("click", async () => {
  if (!otaAvailableVersion) return;
  if (!confirm(`安装固件 ${otaAvailableVersion}？安装期间车辆会强制停止并自动重启。`)) return;
  otaEls.install.disabled = true;
  try {
    const res = await fetch("/api/ota/install", {
      method: "POST",
      headers: authHeaders(),
      body: JSON.stringify({ version: otaAvailableVersion }),
    });
    if (!res.ok) throw new Error(res.status === 401 ? "本地控制 Key 无效" : "版本已变化，请重新检查");
    otaEls.state.textContent = "等待设备安装";
    otaEls.hint.textContent = "请勿断电；页面断开属于设备重启的预期现象。";
    pollOtaStatus();
  } catch (e) {
    otaEls.state.textContent = "安装请求失败";
    otaEls.hint.textContent = e.message;
    otaEls.install.disabled = false;
  }
});

otaEls.later?.addEventListener("click", () => {
  otaAvailableVersion = "";
  otaEls.install.hidden = false;
  otaEls.install.disabled = true;
  otaEls.install.textContent = "已暂不安装";
  otaEls.later.hidden = true;
  otaEls.state.textContent = "已暂不安装";
  otaEls.hint.textContent = "未提交安装请求；之后可再次检查。";
});

function uploadDirectOta() {
  const file = otaEls.directFile?.files?.[0];
  if (!file) {
    otaEls.directState.textContent = "未选择文件";
    otaEls.directHint.textContent = "请先选择 PlatformIO 生成的 firmware.bin。";
    return;
  }
  if (!file.name.endsWith(".bin")) {
    otaEls.directState.textContent = "文件类型不对";
    otaEls.directHint.textContent = "请选择 .bin 固件文件。";
    return;
  }
  if (!confirm(`直传 ${file.name}？上传期间车辆会强制停止并自动重启。`)) return;

  const form = new FormData();
  form.append("firmware", file, file.name);
  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/api/ota/local-upload");
  const key = localApiKey();
  if (key) xhr.setRequestHeader("X-FollowBox-Key", key);
  otaEls.directUpload.disabled = true;
  otaEls.directProgress.value = 0;
  otaEls.directState.textContent = "上传中";
  otaEls.directHint.textContent = "请勿断电；完成后页面断开属于设备重启的预期现象。";

  xhr.upload.onprogress = (event) => {
    if (event.lengthComputable) {
      otaEls.directProgress.value = Math.round((event.loaded / event.total) * 100);
    }
  };
  xhr.onload = () => {
    otaEls.directUpload.disabled = false;
    if (xhr.status >= 200 && xhr.status < 300) {
      otaEls.directProgress.value = 100;
      otaEls.directState.textContent = "重启中";
      otaEls.directHint.textContent = "上传完成，等待设备重新上线。";
      pollOtaStatus();
      return;
    }
    let reason = xhr.statusText || "上传失败";
    try {
      reason = JSON.parse(xhr.responseText).reason || reason;
    } catch (e) {
      /* keep HTTP text */
    }
    otaEls.directState.textContent = "上传失败";
    otaEls.directHint.textContent = xhr.status === 401 ? "本地控制 Key 无效。" : reason;
  };
  xhr.onerror = () => {
    otaEls.directUpload.disabled = false;
    otaEls.directState.textContent = "连接中断";
    otaEls.directHint.textContent = "若固件已写完，设备可能正在重启；否则请重新上传。";
  };
  xhr.send(form);
}

otaEls.directUpload?.addEventListener("click", uploadDirectOta);

refreshOtaStatus();
