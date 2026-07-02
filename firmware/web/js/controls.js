// FollowBox embedded H5 control/event bindings.
// Keep firmware safety ownership server-side; this file only sends existing H5 requests.

// ── Network ──

async function postJson(path, body) {
  try {
    await fetch(path, {
      method: "POST",
      headers: authHeaders(),
      body: JSON.stringify(body),
    });
  } catch (e) {
    /* telemetry already shows offline */
  }
}

function connectWs() {
  if (!location.host) {
    setConn(false);
    return;
  }
  ws = new WebSocket(`ws://${location.host}/ws/state`);
  ws.onopen = () => {
    setConn(true);
    wsRetryDelay = 1000;
    wsRetryCount = 0;
    appendBrowserLog("WebSocket 已连接");
  };
  ws.onclose = () => {
    setConn(false);
    wsRetryCount += 1;
    updateReconnectBanner();
    appendBrowserLog(`WebSocket 已断开，${Math.round(wsRetryDelay / 1000)}s 后重连`, "W");
    if (wsRetryTimer) clearTimeout(wsRetryTimer);
    wsRetryTimer = setTimeout(connectWs, wsRetryDelay);
    // Exponential backoff capped at 5s so we reconnect fast but do not hammer
    // the box while the phone is still re-associating to the FollowBox hotspot.
    wsRetryDelay = Math.min(wsRetryDelay * 1.5, 5000);
  };
  ws.onerror = () => {
    appendBrowserLog("WebSocket 错误", "W");
    ws.close();
  };
  ws.onmessage = (ev) => {
    try {
      renderState(JSON.parse(ev.data));
    } catch (e) {
      appendBrowserLog("收到无法解析的状态帧", "W");
    }
  };
}

function updateReconnectBanner() {
  if (!reconnectEls.text) return;
  reconnectEls.text.textContent =
    wsRetryCount > 3
      ? "与小车断开，仍在重连…请确认手机已连回 FollowBox 热点。"
      : "与小车断开，正在自动重连…";
}

async function pollStateFallback() {
  if (Date.now() - lastStateAt < 2000) {
    return;
  }
  try {
    renderState(await fetchJsonWithTimeout("/api/state"));
    setConn(true);
    stateFallbackUnavailableLogged = false;
  } catch (e) {
    if (!stateFallbackUnavailableLogged) {
      appendBrowserLog(`/api/state 不可用：${e.name === "AbortError" ? "timeout" : e.message}`, "W");
      stateFallbackUnavailableLogged = true;
    }
  }
}

async function refreshLogs() {
  if (!els.logs) return;
  try {
    const body = await fetchJsonWithTimeout("/api/logs");
    if (Array.isArray(body.logs) && body.logs.length) {
      deviceLogs.splice(0, deviceLogs.length, ...body.logs.slice(-120));
    } else if (!deviceLogs.length && !browserLogs.length) {
      deviceLogs.splice(0, deviceLogs.length, "-- 暂无设备日志；等待传感器/视频/控制事件 --");
    }
    logsApiUnavailableLogged = false;
    renderLogs();
  } catch (e) {
    if (!logsApiUnavailableLogged) {
      appendBrowserLog(`/api/logs 不可用：${e.name === "AbortError" ? "timeout" : e.message}`, "W");
      logsApiUnavailableLogged = true;
    }
  }
}

// ── Joystick ──

function sendJog(deadman) {
  postJson("/api/jog", {
    seq: jogSeq++,
    forward: joyForward,
    turn: joyTurn,
    deadman,
    client_time_ms: Date.now(),
  });
}

function startJogLoop() {
  if (jogTimer) return;
  sendJog(true);
  jogTimer = setInterval(() => sendJog(true), 150);
}

function stopJogLoop() {
  if (jogTimer) {
    clearInterval(jogTimer);
    jogTimer = null;
  }
  joyForward = 0;
  joyTurn = 0;
  moveStick(0, 0);
  sendJog(false);
}

function moveStick(x, y) {
  if (!els.stick) return;
  els.stick.style.transform = `translate(${x}px, ${y}px)`;
}

function updateJoystick(ev) {
  const rect = els.joy.getBoundingClientRect();
  const radius = rect.width / 2;
  const max = radius * 0.62;
  let x = ev.clientX - rect.left - radius;
  let y = ev.clientY - rect.top - radius;
  const mag = Math.hypot(x, y);
  if (mag > max) {
    x = (x / mag) * max;
    y = (y / mag) * max;
  }
  moveStick(x, y);
  joyTurn = Math.max(-1, Math.min(1, x / max));
  joyForward = Math.max(-1, Math.min(1, -y / max));
}

function setupJoystick() {
  if (!els.joy) return;
  els.joy.addEventListener("pointerdown", (ev) => {
    ev.preventDefault();
    joyPointerId = ev.pointerId;
    els.joy.setPointerCapture(joyPointerId);
    els.joy.classList.add("pressed");
    updateJoystick(ev);
    startJogLoop();
  });
  els.joy.addEventListener("pointermove", (ev) => {
    if (ev.pointerId !== joyPointerId) return;
    ev.preventDefault();
    updateJoystick(ev);
  });
  const release = (ev) => {
    if (ev.pointerId !== joyPointerId) return;
    ev.preventDefault();
    els.joy.classList.remove("pressed");
    joyPointerId = null;
    stopJogLoop();
  };
  els.joy.addEventListener("pointerup", release);
  els.joy.addEventListener("pointercancel", release);
  els.joy.addEventListener("lostpointercapture", () => {
    els.joy.classList.remove("pressed");
    joyPointerId = null;
    stopJogLoop();
  });
}

// ── WiFi ──

async function refreshWifiStatus() {
  if (!wifiEls.status) return;
  try {
    const res = await fetch("/api/wifi/status");
    const st = await res.json();
    const linkMode = st.net_mode === "link" || st.link_enabled === true;
    updateNetModeUi(linkMode);
    if (!linkMode) {
      wifiEls.status.textContent = st.provisioned ? "热点模式（已存 WiFi）" : "热点模式";
      setTextState(wifiEls.status, true);
      if (wifiEls.ip) wifiEls.ip.textContent = "--";
      return;
    }
    if (!st.provisioned) {
      wifiEls.status.textContent = "联网模式：未配置 WiFi";
      setTextState(wifiEls.status, false, true);
    } else if (st.sta_connected) {
      wifiEls.status.textContent = `${st.ssid} 已连接 (${st.rssi}dBm)`;
      setTextState(wifiEls.status, true);
    } else {
      wifiEls.status.textContent = `${st.ssid} 连接中`;
      setTextState(wifiEls.status, false, true);
    }
    if (wifiEls.ip) {
      wifiEls.ip.textContent = st.sta_connected && st.ip ? st.ip : "--";
    }
  } catch (e) {
    /* keep last status */
  }
}

function updateNetModeUi(linkMode) {
  if (wifiEls.netMode) {
    wifiEls.netMode.textContent = linkMode ? "联网模式" : "热点模式";
  }
  if (wifiEls.btnHotspot) {
    wifiEls.btnHotspot.classList.toggle("fb-net-btn--active", !linkMode);
    wifiEls.btnHotspot.setAttribute("aria-pressed", String(!linkMode));
  }
  if (wifiEls.btnLink) {
    wifiEls.btnLink.classList.toggle("fb-net-btn--active", linkMode);
    wifiEls.btnLink.setAttribute("aria-pressed", String(linkMode));
  }
}

async function switchNetMode(mode) {
  if (wifiSwitching) return;
  if (mode === "link" && !wifiEls.ssid?.value.trim()) {
    // Warn early; the firmware also rejects link mode with no stored WiFi.
    if (!confirm("联网模式需要先保存家庭 WiFi。仍要切换吗？")) return;
  }
  wifiSwitching = true;
  try {
    const res = await fetch("/api/wifi/mode", {
      method: "POST",
      headers: authHeaders(),
      body: JSON.stringify({ mode }),
    });
    if (res.status === 409) {
      alert("请先在下方保存家庭 WiFi，再切换到联网模式。");
    } else if (!res.ok || !(await res.json()).ok) {
      alert("切换失败，请重试。");
    } else if (mode === "link") {
      appendBrowserLog("已切到联网模式，热点可能短暂波动", "W");
      updateNetModeUi(true);
    } else {
      appendBrowserLog("已切回纯热点模式");
      updateNetModeUi(false);
    }
  } catch (e) {
    alert("请求失败，请确认已连接小车热点。");
  }
  wifiSwitching = false;
  setTimeout(refreshWifiStatus, 2500);
}

function bindSafetyStopEvents() {
  document.addEventListener("visibilitychange", () => {
    if (document.hidden && jogTimer) stopJogLoop();
  });
  document.addEventListener("pagehide", () => {
    if (jogTimer) stopJogLoop();
  });
}

function bindModeAndViewEvents() {
  document.querySelectorAll(".fb-mode-btn").forEach((btn) => {
    btn.addEventListener("click", () =>
      postJson("/api/mode-request", { requested_mode: btn.dataset.mode })
    );
  });

  document.querySelectorAll(".fb-nav-btn").forEach((btn) => {
    btn.addEventListener("click", () => switchView(btn.dataset.view));
  });
}

function bindCameraEvents() {
  if (els.cameraStream) {
    els.cameraStream.addEventListener("load", () => {
      setCameraOnline(true, "画面在线");
      appendBrowserLog("视频画面在线");
      cameraRetryDelay = 3000;
      if (cameraRetryTimer) {
        clearTimeout(cameraRetryTimer);
        cameraRetryTimer = null;
      }
    });
    els.cameraStream.addEventListener("error", () => {
      setCameraOnline(false, "画面离线");
      appendBrowserLog("视频加载失败", "W");
      scheduleCameraRetry();
    });
  }

  document.addEventListener("fullscreenchange", handleFullscreenChange);
  document.addEventListener("webkitfullscreenchange", handleFullscreenChange);
  if (els.fullscreenBtn) {
    els.fullscreenBtn.addEventListener("click", (ev) => {
      ev.stopPropagation();
      toggleFullscreen();
    });
  }

  if (els.btnCameraReload) {
    els.btnCameraReload.addEventListener("click", () => {
      activeCameraUrl = "";
      saveCameraOverride(els.cameraUrl.value);
    });
  }
}

function bindSettingsEvents() {
  if (extEls.resetFault) {
    extEls.resetFault.addEventListener("click", () => {
      postJson("/api/reset-fault", { confirm: true });
    });
  }

  if (extEls.btnSaveCal) {
    extEls.btnSaveCal.addEventListener("click", () => {
      const cal = {
        deadband_mv: parseInt(extEls.calDeadband.value, 10),
        min_active_mv: parseInt(extEls.calMinActive.value, 10),
        max_mv: parseInt(extEls.calMax.value, 10),
        module_full_scale_mv: parseInt(extEls.calFullScale.value, 10),
        rise_mv_per_s: parseInt(extEls.calRise.value, 10),
        fall_mv_per_s: parseInt(extEls.calFall.value, 10),
      };
      if (Object.values(cal).some((value) => Number.isNaN(value))) {
        alert("参数必须为有效整数。");
        return;
      }
      if (
        cal.deadband_mv < 0 ||
        cal.min_active_mv <= cal.deadband_mv ||
        cal.max_mv <= cal.min_active_mv ||
        cal.max_mv > cal.module_full_scale_mv
      ) {
        alert("参数关系有误：死区 < 起转 < 最大 <= 满量程。");
        return;
      }
      postJson("/api/calibrate", cal);
    });
  }

  if (extEls.btnWizardComplete) {
    extEls.btnWizardComplete.addEventListener("click", () => {
      const checks = {
        estop_checked: !!extEls.wizEstop?.checked,
        wheels_lifted: !!extEls.wizWheels?.checked,
        direction_checked: !!extEls.wizDirection?.checked,
        throttle_checked: !!extEls.wizThrottle?.checked,
      };
      if (
        !checks.estop_checked ||
        !checks.wheels_lifted ||
        !checks.direction_checked ||
        !checks.throttle_checked
      ) {
        alert("请先确认急停、架空、方向和油门标定步骤。");
        return;
      }
      postJson("/api/wizard-complete", { complete: true, ...checks });
    });
  }

  if (extEls.btnLocalKeySave) {
    extEls.btnLocalKeySave.addEventListener("click", () => {
      sessionStorage.setItem(LOCAL_KEY_STORAGE, extEls.localApiKey.value.trim());
      refreshLocalKeyUi();
      alert("本地控制 Key 已保存（当前标签页有效，关闭后需重新输入）。");
    });
  }

  if (wifiEls.save) {
    wifiEls.save.addEventListener("click", async () => {
      const ssid = wifiEls.ssid.value.trim();
      const pass = wifiEls.pass.value;
      if (!ssid) {
        alert("请输入 WiFi 名称。");
        return;
      }
      if (pass.length > 0 && pass.length < 8) {
        alert("WiFi 密码至少 8 位（开放网络请留空）。");
        return;
      }
      wifiEls.save.disabled = true;
      wifiEls.save.textContent = "保存中";
      try {
        const res = await fetch("/api/wifi", {
          method: "POST",
          headers: authHeaders(),
          body: JSON.stringify({ ssid, password: pass }),
        });
        const ok = res.ok && (await res.json()).ok;
        alert(
          ok
            ? "已保存。切到“联网模式”后小车会连接该 WiFi。"
            : "保存失败，请检查输入。"
        );
      } catch (e) {
        alert("请求失败，请确认已连接小车热点。");
      }
      wifiEls.save.disabled = false;
      wifiEls.save.textContent = "保存 WiFi";
      setTimeout(refreshWifiStatus, 3000);
    });
  }

  if (wifiEls.btnHotspot) {
    wifiEls.btnHotspot.addEventListener("click", () => switchNetMode("hotspot"));
  }
  if (wifiEls.btnLink) {
    wifiEls.btnLink.addEventListener("click", () => switchNetMode("link"));
  }
}

async function copyVisibleLogs() {
  const text = els.logs?.textContent || "";
  const trimmed = text.trim();
  const button = els.copyLogs;
  if (!button) return;
  if (!trimmed || trimmed === "-- 等待日志 --") {
    button.textContent = "无日志";
    setTimeout(() => { button.textContent = "复制"; }, 1200);
    return;
  }
  try {
    if (navigator.clipboard?.writeText) {
      await navigator.clipboard.writeText(text);
    } else {
      const textarea = document.createElement("textarea");
      textarea.value = text;
      textarea.setAttribute("readonly", "");
      textarea.style.position = "fixed";
      textarea.style.left = "-9999px";
      document.body.appendChild(textarea);
      textarea.select();
      const copied = document.execCommand("copy");
      document.body.removeChild(textarea);
      if (!copied) throw new Error("copy command failed");
    }
    button.textContent = "已复制";
  } catch (e) {
    button.textContent = "复制失败";
  }
  setTimeout(() => { button.textContent = "复制"; }, 1200);
}

function bindLogEvents() {
  if (els.clearLogs) {
    els.clearLogs.addEventListener("click", () => {
      deviceLogs.length = 0;
      browserLogs.length = 0;
      renderLogs();
    });
  }
  if (els.copyLogs) {
    els.copyLogs.addEventListener("click", copyVisibleLogs);
  }
}

function bindOnlineEvents() {
  window.addEventListener("online", () => {
    if (!ws || ws.readyState > 1) connectWs();
  });
  window.addEventListener("offline", () => {
    setConn(false);
  });
}

function initControls() {
  bindSafetyStopEvents();
  bindModeAndViewEvents();
  bindCameraEvents();
  bindSettingsEvents();
  bindLogEvents();
  bindOnlineEvents();

  setInterval(pollStateFallback, 1000);
  setInterval(refreshLogs, 2000);
  setInterval(refreshWifiStatus, 5000);
  refreshLogs();
  refreshWifiStatus();
  refreshLocalAuthStatus();
  refreshLocalKeyUi();
  restoreCameraOverride();
  setCameraStream(els.cameraUrl?.value);
  setupJoystick();
  setupCanvasDPI(els.spatialMap);
  setupCanvasDPI(els.uwbCanvas);
  setupCanvasDPI(els.obstacleCanvas);
  drawSpatialMap({});
  drawUwb({});
  drawObstacle({});
  requestAnimationFrame(animateSpatialMap);
  const initialView = (location.hash || "").slice(1);
  if (["drive", "sensors", "status", "settings"].includes(initialView)) {
    switchView(initialView);
  }
  connectWs();
}

initControls();
