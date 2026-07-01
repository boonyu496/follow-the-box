// Cloud console realtime transport, low-speed jog commands, and log actions.
(function initFollowBoxCloudRealtime(global) {
  function initRealtimeControls({
    els,
    apiPath,
    operatorTokenValue,
    render,
    setOnline,
    setConnecting,
    setTextState,
    flashStatus,
    deviceOnlineTtlMs,
  }) {
    let events = null;
    let sseRetryDelay = 2000;
    let sseReconnectTimer = null;
    let sseConnectAttempt = 0;
    let manualReconnectPending = false;
    let jogTimer = null;
    let joyPointerId = null;
    let joyForward = 0;
    let joyTurn = 0;
    let latestLastIngestAt = 0;
    let latestTelemetryFreshAt = 0;
    let sseTransportOpen = false;

    function deviceTelemetryOnline(lastIngestAt) {
      return typeof lastIngestAt === "number" && lastIngestAt > 0 &&
        Date.now() - lastIngestAt < deviceOnlineTtlMs;
    }

    function refreshDeviceConnectionStatus() {
      if (!sseTransportOpen) return;
      const online = latestTelemetryFreshAt > 0 &&
        Date.now() - latestTelemetryFreshAt < deviceOnlineTtlMs;
      applyOnlineState(
        online,
        online ? "小车遥测在线" :
          (latestLastIngestAt > 0 ? "云端已连接，小车遥测已超时" : "云端已连接，等待小车首次上报"),
      );
    }

    function applyOnlineState(online, hint) {
      setOnline(online, hint);
      if (online && manualReconnectPending) {
        flashStatus(els.saveStatus, "✅ 重连成功", true);
        manualReconnectPending = false;
      }
    }

    function noteTelemetry(payload) {
      latestLastIngestAt = Number(payload.lastIngestAt) || 0;
      const serverSaysOnline = typeof payload.online === "boolean"
        ? payload.online : deviceTelemetryOnline(latestLastIngestAt);
      latestTelemetryFreshAt = serverSaysOnline ? Date.now() : 0;
      refreshDeviceConnectionStatus();
      return serverSaysOnline;
    }

    function connectEvents() {
      const attempt = ++sseConnectAttempt;
      if (sseReconnectTimer) {
        clearTimeout(sseReconnectTimer);
        sseReconnectTimer = null;
      }
      if (events) events.close();
      setConnecting("正在重连云端 SSE...");
      const token = operatorTokenValue();
      const source = new EventSource(`${apiPath("events")}?token=${token}`);
      events = source;
      source.onopen = () => {
        if (attempt !== sseConnectAttempt || events !== source) return;
        sseTransportOpen = true;
        sseRetryDelay = 2000;
        setConnecting("SSE 已打开，等待首包遥测...");
      };
      source.onmessage = (ev) => {
        if (attempt !== sseConnectAttempt || events !== source) return;
        try {
          render(JSON.parse(ev.data));
        } catch (error) {
          console.warn("FollowBox telemetry parse failed", error);
          applyOnlineState(false, "云端遥测格式错误，请检查服务器日志");
        }
      };
      source.onerror = () => {
        if (attempt !== sseConnectAttempt || events !== source) return;
        sseTransportOpen = false;
        const delay = sseRetryDelay;
        applyOnlineState(false, `连接失败，${Math.round(delay / 1000)} 秒后自动重试（请检查 Token / 网络 / 设备上报）`);
        if (manualReconnectPending) {
          flashStatus(els.saveStatus, `❌ 重连失败，${Math.round(delay / 1000)} 秒后重试`, false);
          manualReconnectPending = false;
        }
        source.close();
        sseRetryDelay = Math.min(sseRetryDelay * 2, 30000);
        sseReconnectTimer = setTimeout(() => {
          connectEvents();
        }, delay);
      };
    }

    async function sendCommand(body) {
      const headers = { "Content-Type": "application/json" };
      const token = els.operatorToken.value.trim();
      if (token) headers.Authorization = `Bearer ${token}`;
      try {
        const resp = await fetch(apiPath("command"), {
          method: "POST",
          headers,
          body: JSON.stringify(body),
        });
        if (!resp.ok && els.commandStatus) {
          els.commandStatus.textContent = resp.status === 401 ? "命令失败：Token 无效" : `命令失败：HTTP ${resp.status}`;
          setTextState(els.commandStatus, false, false);
        }
      } catch (e) {
        if (els.commandStatus) {
          els.commandStatus.textContent = "命令失败：网络错误";
          setTextState(els.commandStatus, false, false);
        }
      }
    }

    function jogBody() {
      return {
        deadman: !!jogTimer,
        forward: Math.round(joyForward * 100) / 100,
        turn: Math.round(joyTurn * 100) / 100,
      };
    }

    function startJog() {
      if (jogTimer) return;
      els.deadman.classList.add("active");
      sendCommand(jogBody());
      jogTimer = setInterval(() => sendCommand(jogBody()), 150);
    }

    function moveStick(x, y) {
      if (!els.stick) return;
      els.stick.style.transform = `translate(${x}px, ${y}px)`;
    }

    function stopJog() {
      if (jogTimer) clearInterval(jogTimer);
      jogTimer = null;
      els.deadman.classList.remove("active");
      joyForward = 0;
      joyTurn = 0;
      moveStick(0, 0);
      sendCommand({ deadman: false, forward: 0, turn: 0 });
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

    function initJoystick() {
      if (!els.joy) return;
      els.joy.addEventListener("pointerdown", (ev) => {
        ev.preventDefault();
        joyPointerId = ev.pointerId;
        els.joy.setPointerCapture(joyPointerId);
        els.joy.classList.add("pressed");
        updateJoystick(ev);
        startJog();
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
        stopJog();
      };
      els.joy.addEventListener("pointerup", release);
      els.joy.addEventListener("pointercancel", release);
      els.joy.addEventListener("lostpointercapture", () => {
        els.joy.classList.remove("pressed");
        joyPointerId = null;
        stopJog();
      });
    }

    ["mousedown", "touchstart"].forEach((eventName) =>
      els.deadman.addEventListener(eventName, (ev) => { ev.preventDefault(); startJog(); })
    );
    ["mouseup", "mouseleave", "touchend", "touchcancel"].forEach((eventName) =>
      els.deadman.addEventListener(eventName, (ev) => { ev.preventDefault(); stopJog(); })
    );
    els.safeIdle.addEventListener("click", () =>
      sendCommand({ safe_idle: true, deadman: false, forward: 0, turn: 0 })
    );
    document.addEventListener("visibilitychange", () => {
      if (document.hidden && jogTimer) stopJog();
    });
    document.addEventListener("pagehide", () => { if (jogTimer) stopJog(); });

    els.deviceId.addEventListener("change", connectEvents);
    els.operatorToken.addEventListener("change", connectEvents);
    global.addEventListener("online", () => {
      connectEvents();
    });
    global.addEventListener("offline", () => {
      sseTransportOpen = false;
      applyOnlineState(false);
    });
    global.setInterval(refreshDeviceConnectionStatus, 1000);

    return {
      connectEvents,
      initJoystick,
      isJogging: () => !!jogTimer,
      markManualReconnectPending: () => { manualReconnectPending = true; },
      noteTelemetry,
      refreshDeviceConnectionStatus,
      sendCommand,
      stopJog,
    };
  }

  function initCloudLogTools({ els }) {
    async function copyVisibleLogs() {
      const text = els.logs.textContent || "";
      const trimmed = text.trim();
      const button = els.copyLogs;
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

    const pageLogBuffer = [];
    const maxPageLogs = 300;

    function appendPageLog(level, argsArr) {
      const ts = new Date().toLocaleTimeString("zh-CN", { hour12: false });
      const text = argsArr.map((a) => {
        try {
          return typeof a === "object" && a !== null ? JSON.stringify(a) : String(a);
        } catch (_e) { return String(a); }
      }).join(" ");
      pageLogBuffer.push(`[${ts}][${level}] ${text}`);
      if (pageLogBuffer.length > maxPageLogs) pageLogBuffer.shift();
      if (els.browserLogs) {
        els.browserLogs.textContent = pageLogBuffer.join("\n");
        els.browserLogs.scrollTop = els.browserLogs.scrollHeight;
      }
    }

    if (els.copyLogs) els.copyLogs.addEventListener("click", copyVisibleLogs);
    if (els.clearLogs) {
      els.clearLogs.addEventListener("click", () => { els.logs.textContent = ""; });
    }

    ["log", "info", "warn", "error"].forEach((level) => {
      const orig = console[level].bind(console);
      console[level] = (...args) => { orig(...args); appendPageLog(level.toUpperCase(), args); };
    });

    global.addEventListener("error", (ev) => {
      appendPageLog("RUNTIME", [`${ev.message} @${ev.filename}:${ev.lineno}`]);
    });
    global.addEventListener("unhandledrejection", (ev) => {
      appendPageLog("PROMISE", [String(ev.reason)]);
    });

    if (els.copyBrowserLogs) {
      els.copyBrowserLogs.addEventListener("click", async () => {
        const text = pageLogBuffer.join("\n");
        try {
          await navigator.clipboard.writeText(text);
          els.copyBrowserLogs.textContent = "已复制";
        } catch (_e) { els.copyBrowserLogs.textContent = "复制失败"; }
        setTimeout(() => { els.copyBrowserLogs.textContent = "复制"; }, 1200);
      });
    }
    if (els.clearBrowserLogs) {
      els.clearBrowserLogs.addEventListener("click", () => {
        pageLogBuffer.length = 0;
        if (els.browserLogs) els.browserLogs.textContent = "";
      });
    }

    return {
      appendPageLog,
    };
  }

  global.FollowBoxCloudRealtime = Object.freeze({
    initCloudLogTools,
    initRealtimeControls,
  });
})(window);
