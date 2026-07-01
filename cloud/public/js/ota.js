// Cloud OTA panel behavior. Checks are read-only; install still requires user consent.
(function initFollowBoxCloudOta(global) {
  function initOtaPanel({ els, apiPath, flashStatus, setSaveStatus }) {
    let checkedOtaVersion = "";

    async function checkOtaVersion() {
      if (!els.otaVersion) return;
      try {
        els.otaVersion.textContent = "检查中...";
        els.otaStatus.textContent = "";
        const token = els.operatorToken.value.trim();
        const headers = {};
        if (token) headers.Authorization = `Bearer ${token}`;
        const resp = await fetch(apiPath("firmware/version"), { headers, cache: "no-store" });
        if (resp.status === 404) {
          els.otaVersion.textContent = "未发布";
          checkedOtaVersion = "";
          flashStatus(els.otaStatus, "暂无固件更新", true);
          return;
        }
        if (!resp.ok) {
          els.otaVersion.textContent = "获取失败";
          flashStatus(els.otaStatus, "❌ " + (resp.status === 401 ? "Token 无效" : "服务器错误"), false);
          return;
        }
        const data = await resp.json();
        if (data.ok) {
          els.otaCurrentVersion.textContent = data.current_version || "--";
          els.otaVersion.textContent = data.available_version || data.version || "--";
          checkedOtaVersion = data.update_available ? data.available_version : "";
          els.installOta.hidden = !checkedOtaVersion;
          els.laterOta.hidden = !checkedOtaVersion;
          if (!data.current_version) {
            setSaveStatus(els.otaStatus, "设备离线或尚未上报版本，不能安装", "warn");
          } else if (data.update_available) {
            setSaveStatus(els.otaStatus, `发现新版本 ${data.available_version}，请选择安装或暂不安装`, "warn");
          } else {
            flashStatus(els.otaStatus, "已是最新版本", true);
          }
        } else {
          els.otaVersion.textContent = "未发布";
          flashStatus(els.otaStatus, "暂无固件更新", true);
        }
      } catch (e) {
        els.otaVersion.textContent = "网络错误";
        flashStatus(els.otaStatus, "❌ 无法连接服务器", false);
      }
    }

    async function installCheckedOta() {
      if (!checkedOtaVersion) return;
      if (!global.confirm(`安装固件 ${checkedOtaVersion}？设备将强制停车、写入校验并自动重启。`)) return;
      const token = els.operatorToken.value.trim();
      const headers = { "Content-Type": "application/json" };
      if (token) headers.Authorization = `Bearer ${token}`;
      els.installOta.disabled = true;
      try {
        const resp = await fetch(apiPath("firmware/install"), {
          method: "POST",
          headers,
          body: JSON.stringify({ version: checkedOtaVersion }),
        });
        const data = await resp.json();
        if (!resp.ok || !data.ok) throw new Error(data.reason || `HTTP ${resp.status}`);
        setSaveStatus(els.otaStatus, "安装请求已提交，等待设备拉取", "warn");
        els.laterOta.hidden = true;
      } catch (e) {
        setSaveStatus(els.otaStatus, `安装请求失败：${e.message}`, "err");
        els.installOta.disabled = false;
      }
    }

    function deferCheckedOta() {
      checkedOtaVersion = "";
      els.installOta.hidden = true;
      els.laterOta.hidden = true;
      setSaveStatus(els.otaStatus, "已暂不安装；未向设备提交任何写入请求", "ok");
    }

    if (els.checkOta) els.checkOta.addEventListener("click", checkOtaVersion);
    if (els.installOta) els.installOta.addEventListener("click", installCheckedOta);
    if (els.laterOta) els.laterOta.addEventListener("click", deferCheckedOta);

    return {
      checkOtaVersion,
    };
  }

  global.FollowBoxCloudOta = Object.freeze({
    initOtaPanel,
  });
})(window);
