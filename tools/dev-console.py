#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
FollowBox Dev Console — 零依赖 Python 后端
Git 推拉 + 云端部署 + ESP32 OTA，一个文件搞定。
启动: python tools/dev-console.py  然后浏览器打开 http://localhost:7777
"""
import argparse
import http.server
import json
import os
import shlex
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
import webbrowser
import hashlib
import re
from urllib.parse import urlparse, parse_qs

# ─── 配置 ────────────────────────────────────────────
if getattr(sys, 'frozen', False):
    PROJECT_ROOT = os.path.dirname(os.path.abspath(sys.executable))
    TOOLS_DIR = os.path.join(PROJECT_ROOT, "tools")
else:
    TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
    PROJECT_ROOT = os.path.dirname(TOOLS_DIR)

CONFIG_PATH = os.path.join(TOOLS_DIR, "followbox-control-center.config.json")
DEFAULT_PORT = 7777
DEFAULT_REMOTE_URL = "git@github.com:boonyu496/follow-the-box.git"
DEFAULT_CLOUD_URL = "https://www.boonai.cn/fb/"
PM2_NAME = "followbox-cloud"
DEVICE_TOKEN = os.environ.get("FOLLOWBOX_DEVICE_TOKEN", "f892ef460de624143d7d65cb5a863f84")
DEVICE_ID = "followbox-001"
OTA_AUTH = os.environ.get("FOLLOWBOX_OTA_AUTH", "followbox-ota")


def ensure_trailing_slash(value):
    text = (value or "").strip()
    if not text:
        return ""
    return text if text.endswith("/") else text + "/"


def load_runtime_config():
    config = {
        "repoPath": PROJECT_ROOT,
        "gitRemote": "origin",
        "cloneUrl": DEFAULT_REMOTE_URL,
        "cloudPath": os.path.join(PROJECT_ROOT, "cloud"),
        "cloudHost": "82.156.85.60",
        "cloudPort": 51400,
        "cloudUser": "root",
        "cloudPemPath": "",
        "cloudRemoteDir": "/www/wwwroot/followbox-cloud",
        "cloudVerifyUrl": DEFAULT_CLOUD_URL,
        "firmwarePath": os.path.join(PROJECT_ROOT, "firmware"),
        "cloudFirmwarePath": os.path.join(PROJECT_ROOT, "cloud", "firmware"),
        "pioCommand": "pio",
        "firmwareEnv": "esp32-s3-devkitc-1",
        "otaEnv": "ota",
        "otaUploadPort": "192.168.4.1",
    }
    if os.path.isfile(CONFIG_PATH):
        try:
            with open(CONFIG_PATH, "r", encoding="utf-8-sig") as f:
                loaded = json.load(f)
            if isinstance(loaded, dict):
                for key, value in loaded.items():
                    if value is None:
                        continue
                    if isinstance(value, str) and not value.strip() and isinstance(config.get(key), str) and config.get(key):
                        continue
                    config[key] = value
        except Exception:
            pass
    config["cloudVerifyUrl"] = ensure_trailing_slash(config.get("cloudVerifyUrl") or DEFAULT_CLOUD_URL)
    return config


CONFIG = load_runtime_config()
REPO_ROOT = os.path.abspath(CONFIG.get("repoPath") or PROJECT_ROOT)
FIRMWARE_DIR = os.path.abspath(CONFIG.get("firmwarePath") or os.path.join(REPO_ROOT, "firmware"))
CLOUD_DIR = os.path.abspath(CONFIG.get("cloudPath") or os.path.join(REPO_ROOT, "cloud"))
CLOUD_FIRMWARE_DIR = os.path.abspath(CONFIG.get("cloudFirmwarePath") or os.path.join(CLOUD_DIR, "firmware"))
GIT_REMOTE = str(CONFIG.get("gitRemote") or "origin")
CLONE_URL = str(CONFIG.get("cloneUrl") or DEFAULT_REMOTE_URL)
CLOUD_HOST = str(CONFIG.get("cloudHost") or "82.156.85.60")
CLOUD_PORT = str(CONFIG.get("cloudPort") or "22")
CLOUD_USER = str(CONFIG.get("cloudUser") or "root")
CLOUD_PEM_PATH = str(CONFIG.get("cloudPemPath") or "")
CLOUD_PATH = str(CONFIG.get("cloudRemoteDir") or "/www/wwwroot/followbox-cloud")
CLOUD_URL = ensure_trailing_slash(CONFIG.get("cloudVerifyUrl") or DEFAULT_CLOUD_URL)
PIO_COMMAND = str(CONFIG.get("pioCommand") or "pio")
FIRMWARE_ENV = str(CONFIG.get("firmwareEnv") or "esp32-s3-devkitc-1")
OTA_ENV = str(CONFIG.get("otaEnv") or "ota")
OTA_IP = str(CONFIG.get("otaUploadPort") or "192.168.4.1")
OTA_PORT = "3232"

# ─── 工具函数 ────────────────────────────────────────
def run(cmd, cwd=None, timeout=120, shell=None):
    """执行命令，返回 dict{ok, stdout, stderr, code}"""
    if shell is None:
        shell = isinstance(cmd, str)
    try:
        r = subprocess.run(
            cmd,
            shell=shell,
            cwd=cwd,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
        )
        return {"ok": r.returncode == 0, "stdout": r.stdout, "stderr": r.stderr, "code": r.returncode, "cmd": cmd}
    except subprocess.TimeoutExpired:
        return {"ok": False, "stdout": "", "stderr": f"超时 ({timeout}s)", "code": -1, "cmd": cmd}
    except Exception as e:
        return {"ok": False, "stdout": "", "stderr": str(e), "code": -1, "cmd": cmd}

def md5_file(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            h.update(chunk)
    return h.hexdigest()

def git(args, cwd=None, timeout=120):
    cwd = cwd or REPO_ROOT
    return run(["git", *args], cwd=cwd, timeout=timeout, shell=False)


def git_current_branch(cwd=None):
    result = git(["branch", "--show-current"], cwd=cwd)
    branch = result["stdout"].strip()
    return {"ok": result["ok"] and bool(branch), "branch": branch, "result": result}


def combine_output(*parts):
    lines = []
    for part in parts:
        if not part:
            continue
        text = str(part).strip()
        if text:
            lines.append(text)
    return "\n".join(lines)


def collect_result_text(label, result):
    if not result:
        return ""
    header = f"[{label}]"
    return combine_output(header, result.get("stdout", ""), result.get("stderr", ""))


def ensure_remote_target():
    if not CLOUD_HOST:
        raise RuntimeError("未配置 cloudHost，无法连接云端服务器。")
    return f"{CLOUD_USER}@{CLOUD_HOST}" if CLOUD_USER else CLOUD_HOST


def ssh_args():
    args = ["-o", "StrictHostKeyChecking=accept-new"]
    if CLOUD_PEM_PATH:
        args.extend(["-i", CLOUD_PEM_PATH])
    if CLOUD_PORT:
        args.extend(["-p", str(CLOUD_PORT)])
    return args


def scp_args():
    args = ["-o", "StrictHostKeyChecking=accept-new"]
    if CLOUD_PEM_PATH:
        args.extend(["-i", CLOUD_PEM_PATH])
    if CLOUD_PORT:
        args.extend(["-P", str(CLOUD_PORT)])
    return args


def ssh_run(cmd_str, timeout=60):
    remote = ensure_remote_target()
    remote_cmd = f"bash -lc {shlex.quote(cmd_str)}"
    return run(["ssh", *ssh_args(), remote, remote_cmd], timeout=timeout, shell=False)


def scp_upload(local, remote_dir):
    remote = ensure_remote_target()
    target = f"{remote}:{remote_dir.rstrip('/')}/"
    return run(["scp", *scp_args(), "-r", local, target], timeout=120, shell=False)


def safe_pull_repo(repo_dir, remote_name):
    branch_info = git_current_branch(cwd=repo_dir)
    if not branch_info["ok"]:
        result = branch_info["result"]
        return {
            "ok": False,
            "stdout": "",
            "stderr": combine_output("无法识别当前分支。", result.get("stderr", ""), result.get("stdout", "")),
            "branch": "",
            "stashed": False,
        }

    branch = branch_info["branch"]
    status = git(["status", "--porcelain"], cwd=repo_dir)
    has_changes = bool(status["stdout"].strip())
    stash_result = None
    if has_changes:
        stash_result = git(["stash", "push", "-u", "-m", "auto-stash-before-pull-from-repo"], cwd=repo_dir, timeout=180)
        if not stash_result["ok"]:
            return {
                "ok": False,
                "stdout": collect_result_text("status", status) + "\n" + collect_result_text("stash", stash_result),
                "stderr": "自动暂存本地改动失败，已取消 pull。",
                "branch": branch,
                "stashed": False,
            }

    pull_result = git(["pull", "--ff-only", remote_name, branch], cwd=repo_dir, timeout=180)
    pop_result = None
    if stash_result:
        pop_result = git(["stash", "pop"], cwd=repo_dir, timeout=180)

    stdout = combine_output(
        collect_result_text("status", status),
        collect_result_text("stash", stash_result),
        collect_result_text("pull", pull_result),
        collect_result_text("stash-pop", pop_result),
    )
    stderr = combine_output(
        "" if pull_result["ok"] else "Pull 失败。",
        "" if (pop_result is None or pop_result["ok"]) else "恢复暂存改动时出现冲突，请手动检查。",
    )
    ok = pull_result["ok"] and (pop_result is None or pop_result["ok"])
    return {
        "ok": ok,
        "stdout": stdout,
        "stderr": stderr,
        "branch": branch,
        "stashed": bool(stash_result),
    }

# ─── API 路由 ────────────────────────────────────────
def api_git_status(params):
    r = git(["status", "-s", "-b"])
    branch_r = git(["branch", "-vv"])
    # clean = 除 ## 分支头外没有其他行
    lines = [l for l in r["stdout"].strip().split("\n") if l.strip()]
    return {
        "ok": r["ok"],
        "status": r["stdout"],
        "branch": branch_r["stdout"],
        "clean": len(lines) <= 1,
    }

def api_git_log(params):
    n = params.get("n", ["20"])[0]
    r = git(["log", "--oneline", "--graph", f"-{n}", "--all"])
    return {"ok": r["ok"], "log": r["stdout"], "stderr": r["stderr"]}

def api_git_diff(params):
    r = git(["diff", "HEAD"])
    return {"ok": r["ok"], "diff": r["stdout"][:50000], "stderr": r["stderr"]}

def api_git_push(params):
    branch_info = git_current_branch()
    if not branch_info["ok"]:
        result = branch_info["result"]
        return {"ok": False, "stdout": result["stdout"], "stderr": combine_output("无法识别当前分支。", result["stderr"])}
    branch = branch_info["branch"]
    r = git(["push", "--set-upstream", GIT_REMOTE, branch], timeout=180)
    return {"ok": r["ok"], "stdout": combine_output(f"当前分支: {branch}", r["stdout"]), "stderr": r["stderr"], "branch": branch}

def api_git_pull(params):
    return safe_pull_repo(REPO_ROOT, GIT_REMOTE)

def api_git_commit(params):
    msg = params.get("msg", [""])[0]
    if not msg:
        return {"ok": False, "stderr": "请填写 commit message"}
    add_result = git(["add", "-A"])
    if not add_result["ok"]:
        return {"ok": False, "stdout": add_result["stdout"], "stderr": add_result["stderr"]}
    # 用临时文件传参，彻底避免 shell 注入
    with tempfile.NamedTemporaryFile(mode="w", suffix=".txt", delete=False, encoding="utf-8") as f:
        f.write(msg)
        tmp = f.name
    try:
        r = git(["commit", "-F", tmp], timeout=180)
    finally:
        os.unlink(tmp)
    return {"ok": r["ok"], "stdout": combine_output(add_result["stdout"], r["stdout"]), "stderr": combine_output(add_result["stderr"], r["stderr"])}

def api_git_fetch(params):
    r = git(["fetch", GIT_REMOTE, "--prune"], timeout=180)
    return {"ok": r["ok"], "stdout": r["stdout"], "stderr": r["stderr"]}

def api_git_clone(params):
    """克隆仓库到指定本地路径"""
    raw_dest = params.get("dest", [""])[0]
    if not raw_dest:
        return {"ok": False, "stderr": "请指定目标文件夹路径"}
    dest = os.path.abspath(raw_dest)
    url = params.get("url", [CLONE_URL])[0]
    if os.path.isdir(dest) and os.listdir(dest):
        return {"ok": False, "stderr": f"目标目录非空，不能直接克隆: {dest}"}
    r = run(["git", "clone", url, dest], shell=False, timeout=180)
    return {"ok": r["ok"], "stdout": r["stdout"], "stderr": r["stderr"]}

def api_git_download(params):
    """下载/拉取代码到本地指定文件夹 (git clone or pull if exists)"""
    raw_dest = params.get("dest", [""])[0]
    if not raw_dest:
        return {"ok": False, "stderr": "请指定目标文件夹路径"}
    dest = os.path.abspath(raw_dest)
    url = CLONE_URL
    if os.path.isdir(os.path.join(dest, ".git")):
        result = safe_pull_repo(dest, "origin")
        result["stdout"] = combine_output(f"已有仓库，目标目录: {dest}", result["stdout"])
        return result
    if os.path.isdir(dest) and os.listdir(dest):
        return {"ok": False, "stderr": f"目标目录非空且不是 Git 仓库，已取消: {dest}"}
    r = run(["git", "clone", url, dest], shell=False, timeout=180)
    return {"ok": r["ok"], "stdout": combine_output(f"克隆到 {dest}", r["stdout"]), "stderr": r["stderr"]}

def api_git_branches(params):
    r = git(["branch", "-a"])
    return {"ok": r["ok"], "branches": r["stdout"], "stderr": r["stderr"]}

def api_deploy_status(params):
    """查看云端服务器当前部署状态"""
    r = ssh_run(f"cat {CLOUD_PATH}/public/deploy-version.txt 2>/dev/null; echo '==='; ls -la {CLOUD_PATH}/firmware/ 2>/dev/null; echo '==='; pm2 describe {PM2_NAME} 2>/dev/null | grep -E 'status|uptime|restarts|script path'")
    return {"ok": r["ok"], "info": r["stdout"], "stderr": r["stderr"]}

def api_deploy_cloud(params):
    """部署 cloud/ 目录到服务器并重启 PM2"""
    steps = []
    # 0. 更新 deploy-version.txt 时间戳
    version_file = os.path.join(CLOUD_DIR, "public", "deploy-version.txt")
    try:
        with open(version_file, "r", encoding="utf-8") as f:
            content = f.read()
        now_iso = time.strftime("%Y-%m-%dT%H:%M:%S+08:00")
        content = re.sub(r"built_at=.*", f"built_at={now_iso}", content)
        with open(version_file, "w", encoding="utf-8") as f:
            f.write(content)
    except Exception:
        pass  # 文件不存在不阻塞部署
    # 1. 依次上传文件，任一失败立即中止
    uploads = [
        ("server.js", os.path.join(CLOUD_DIR, "server.js")),
        ("public/", os.path.join(CLOUD_DIR, "public")),
        ("package.json", os.path.join(CLOUD_DIR, "package.json")),
        ("deploy-clean-cache.sh", os.path.join(CLOUD_DIR, "deploy-clean-cache.sh")),
    ]
    for label, local_path in uploads:
        r = scp_upload(local_path, CLOUD_PATH)
        ok = r["ok"]
        steps.append({"step": f"上传 {label}", "ok": ok, "msg": r["stdout"] + r["stderr"]})
        if not ok:
            return {"ok": False, "steps": steps, "error": f"上传 {label} 失败，已中止"}
    # 2. 执行部署脚本 (清缓存 + pm2 restart + nginx reload)
    r5 = ssh_run(f"cd {CLOUD_PATH} && bash deploy-clean-cache.sh", timeout=30)
    steps.append({"step": "执行部署脚本", "ok": r5["ok"], "msg": r5["stdout"] + r5["stderr"]})
    all_ok = all(s["ok"] for s in steps)
    return {"ok": all_ok, "steps": steps}

def api_deploy_firmware_to_cloud(params):
    """编译固件并上传 bin+manifest 到云端服务器"""
    steps = []
    # 1. 编译固件
    r1 = run([PIO_COMMAND, "run"], cwd=FIRMWARE_DIR, timeout=300, shell=False)
    steps.append({"step": "编译固件 (pio run)", "ok": r1["ok"], "msg": r1["stdout"][-2000:] + r1["stderr"][-1000:]})
    if not r1["ok"]:
        return {"ok": False, "steps": steps}
    # 2. 找到 .bin — 锁定 default_envs 对应的编译产物，不靠 os.walk 随机碰
    PIO_DEFAULT_ENV = FIRMWARE_ENV
    bin_path = os.path.join(FIRMWARE_DIR, ".pio", "build", PIO_DEFAULT_ENV, "firmware.bin")
    if not os.path.isfile(bin_path):
        return {"ok": False, "steps": steps, "error": f"找不到 {bin_path}，请先 pio run 编译"}
    # 3. 计算 md5 + size
    md5 = md5_file(bin_path)
    size = os.path.getsize(bin_path)
    # 版本号：日期 + 秒数自增（同一天多次编译不会重复）
    version = time.strftime("%Y.%m.%d") + "." + str(int(time.time()) % 86400)
    manifest = {"version": version, "file": "firmware.bin", "md5": md5, "size": size, "force": False}
    manifest_local = os.path.join(CLOUD_DIR, "firmware", "manifest.json")
    bin_local = os.path.join(CLOUD_DIR, "firmware", "firmware.bin")
    os.makedirs(os.path.dirname(manifest_local), exist_ok=True)
    shutil.copy2(bin_path, bin_local)
    with open(manifest_local, "w") as f:
        json.dump(manifest, f, indent=2)
    steps.append({"step": "生成 manifest", "ok": True, "msg": json.dumps(manifest, indent=2)})
    # 4. 上传到服务器 — 任一失败立即中止
    r4 = scp_upload(bin_local, f"{CLOUD_PATH}/firmware")
    steps.append({"step": "上传 firmware.bin", "ok": r4["ok"], "msg": r4["stdout"] + r4["stderr"]})
    if not r4["ok"]:
        return {"ok": False, "steps": steps, "error": "上传 firmware.bin 失败，已中止", "manifest": manifest}
    r5 = scp_upload(manifest_local, f"{CLOUD_PATH}/firmware")
    steps.append({"step": "上传 manifest.json", "ok": r5["ok"], "msg": r5["stdout"] + r5["stderr"]})
    if not r5["ok"]:
        return {"ok": False, "steps": steps, "error": "上传 manifest.json 失败，已中止", "manifest": manifest}
    # 5. 重启 PM2
    r6 = ssh_run(f"pm2 restart {PM2_NAME} --update-env", timeout=15)
    steps.append({"step": "重启 PM2", "ok": r6["ok"], "msg": r6["stdout"] + r6["stderr"]})
    all_ok = all(s["ok"] for s in steps)
    return {"ok": all_ok, "steps": steps, "manifest": manifest}

def api_ota_wifi(params):
    """WiFi OTA 直接刷到 ESP32 (192.168.4.1:3232)"""
    steps = []
    # 1. 编译
    r1 = run([PIO_COMMAND, "run"], cwd=FIRMWARE_DIR, timeout=300, shell=False)
    steps.append({"step": "编译固件", "ok": r1["ok"], "msg": r1["stdout"][-2000:] + r1["stderr"][-1000:]})
    if not r1["ok"]:
        return {"ok": False, "steps": steps}
    # 2. OTA 上传
    r2 = run([PIO_COMMAND, "run", "-t", "upload", "--environment", OTA_ENV], cwd=FIRMWARE_DIR, timeout=120, shell=False)
    steps.append({"step": "WiFi OTA 上传", "ok": r2["ok"], "msg": r2["stdout"][-2000:] + r2["stderr"][-1000:]})
    all_ok = all(s["ok"] for s in steps)
    return {"ok": all_ok, "steps": steps}

def api_ota_cloud_trigger(params):
    """触发云端 OTA (设置 force=true 让 ESP32 立即拉取)"""
    r = ssh_run(f"cd {CLOUD_PATH} && python3 -c \""
                f"import json;"
                f"m=json.load(open('firmware/manifest.json'));"
                f"m['force']=True;"
                f"json.dump(m,open('firmware/manifest.json','w'),indent=2);"
                f"print(json.dumps(m))\"")
    if r["ok"]:
        ssh_run(f"pm2 restart {PM2_NAME} --update-env")
    return {"ok": r["ok"], "stdout": r["stdout"], "stderr": r["stderr"]}

def api_ota_cloud_status(params):
    """查看云端固件 manifest"""
    r = ssh_run(f"cat {CLOUD_PATH}/firmware/manifest.json")
    return {"ok": r["ok"], "manifest": r["stdout"], "stderr": r["stderr"]}

def api_ota_check_device(params):
    """检查 ESP32 设备是否真正在线 — 多重验证防代理假阳性"""
    # 1. ARP 检查：真实的局域网设备必须有 ARP 记录
    arp_r = run(f"arp -a {OTA_IP}")
    arp_ok = OTA_IP in arp_r["stdout"] and "No ARP" not in arp_r["stdout"]
    # 2. ping 检查：ICMP 必须可达（排除代理 TCP 劫持）
    ping_r = run(f"ping -n 1 -w 1000 {OTA_IP}")
    ping_ok = "TTL=" in ping_r["stdout"]
    # 3. TCP 连接 + 数据验证：连上后必须收到非空响应
    tcp_ok = False
    tcp_msg = ""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(3)
        s.connect((OTA_IP, int(OTA_PORT)))
        # ESP32 ArduinoOTA 在连接后会等待认证，先发认证试试
        s.sendall((OTA_AUTH + "\n").encode())
        time.sleep(0.5)
        try:
            data = s.recv(256)
            tcp_ok = len(data) > 0
            tcp_msg = f"收到 {len(data)} 字节"
        except socket.timeout:
            tcp_msg = "连接成功但无数据返回（可能是代理劫持）"
        s.close()
    except Exception as e:
        tcp_msg = f"连接失败: {e}"
    # 综合判定：ARP + ping + TCP 三项至少两项通过才算在线
    score = sum([arp_ok, ping_ok, tcp_ok])
    online = score >= 2
    return {
        "ok": True, "online": online,
        "arp_ok": arp_ok, "ping_ok": ping_ok, "tcp_ok": tcp_ok,
        "score": f"{score}/3",
        "msg": f"设备 {OTA_IP}:{OTA_PORT} {'在线' if online else '离线'} (ARP:{arp_ok} ping:{ping_ok} TCP:{tcp_ok} → {score}/3)"
    }

def api_cloud_test(params):
    """测试云端服务是否正常"""
    url = f"{CLOUD_URL}api/device/{DEVICE_ID}/command?token={DEVICE_TOKEN}"
    try:
        with urllib.request.urlopen(url, timeout=10) as resp:
            code = str(resp.getcode())
        return {"ok": code == "200", "http_code": code, "url": url}
    except urllib.error.HTTPError as e:
        return {"ok": False, "http_code": str(e.code), "url": url, "stderr": str(e)}
    except Exception as e:
        return {"ok": False, "http_code": "ERR", "url": url, "stderr": str(e)}

def api_pio_info(params):
    """PlatformIO 环境信息"""
    r = run([PIO_COMMAND, "--version"], cwd=FIRMWARE_DIR, shell=False)
    try:
        with open(os.path.join(FIRMWARE_DIR, "platformio.ini"), "r", encoding="utf-8") as f:
            ini_text = f.read()
        ini_err = ""
    except Exception as e:
        ini_text = ""
        ini_err = str(e)
    return {"ok": r["ok"], "version": r["stdout"], "envs": ini_text, "stderr": combine_output(r["stderr"], ini_err)}

# ─── 路由表 ──────────────────────────────────────────
ROUTES = {
    "git/status":      api_git_status,
    "git/log":         api_git_log,
    "git/diff":        api_git_diff,
    "git/push":        api_git_push,
    "git/pull":        api_git_pull,
    "git/commit":      api_git_commit,
    "git/fetch":       api_git_fetch,
    "git/clone":       api_git_clone,
    "git/download":    api_git_download,
    "git/branches":    api_git_branches,
    "deploy/status":   api_deploy_status,
    "deploy/cloud":    api_deploy_cloud,
    "deploy/firmware": api_deploy_firmware_to_cloud,
    "ota/wifi":        api_ota_wifi,
    "ota/cloud/trigger": api_ota_cloud_trigger,
    "ota/cloud/status":  api_ota_cloud_status,
    "ota/check":       api_ota_check_device,
    "cloud/test":      api_cloud_test,
    "pio/info":        api_pio_info,
}

# ─── HTTP Handler ────────────────────────────────────
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass  # 静默

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path.strip("/")
        params = parse_qs(parsed.query)

        if path == "" or path == "index.html":
            self._serve_html()
        elif path.startswith("api/"):
            route = path[4:]
            fn = ROUTES.get(route)
            if fn:
                try:
                    result = fn(params)
                except Exception as e:
                    result = {"ok": False, "error": str(e)}
                self._json(result)
            else:
                self._json({"ok": False, "error": f"未知路由: {route}"}, 404)
        else:
            self._json({"error": "not found"}, 404)

    def do_POST(self):
        parsed = urlparse(self.path)
        path = parsed.path.strip("/")
        if path.startswith("api/"):
            route = path[4:]
            fn = ROUTES.get(route)
            if fn:
                length = int(self.headers.get("Content-Length", 0))
                body = self.rfile.read(length).decode("utf-8") if length else ""
                params = parse_qs(body) if body else {}
                try:
                    result = fn(params)
                except Exception as e:
                    result = {"ok": False, "error": str(e)}
                self._json(result)
            else:
                self._json({"ok": False, "error": f"未知路由: {route}"}, 404)
        else:
            self._json({"error": "not found"}, 404)

    def _json(self, data, code=200):
        body = json.dumps(data, ensure_ascii=False, default=str).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _serve_html(self):
        # PyInstaller 打包后 html 在 _MEIPASS 临时目录
        if getattr(sys, 'frozen', False):
            html_path = os.path.join(sys._MEIPASS, "dev-console.html")
        else:
            html_path = os.path.join(TOOLS_DIR, "dev-console.html")
        with open(html_path, "r", encoding="utf-8") as f:
            body = f.read().encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

def parse_args():
    parser = argparse.ArgumentParser(description="FollowBox Dev Console")
    parser.add_argument("--port", type=int, default=int(os.environ.get("FOLLOWBOX_DEV_CONSOLE_PORT", DEFAULT_PORT)))
    parser.add_argument(
        "--no-browser",
        action="store_true",
        default=os.environ.get("FOLLOWBOX_NO_BROWSER", "").strip().lower() in {"1", "true", "yes", "on"},
        help="启动后不自动打开浏览器",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    print(f"FollowBox Dev Console 启动中...")
    print(f"  项目根目录: {REPO_ROOT}")
    print(f"  固件目录:   {FIRMWARE_DIR}")
    print(f"  云端地址:   {CLOUD_URL}")
    print(f"  OTA 目标:   {OTA_IP}:{OTA_PORT}")
    print(f"  浏览器打开: http://localhost:{args.port}")
    server = http.server.HTTPServer(("127.0.0.1", args.port), Handler)
    def delayed_open():
        time.sleep(1)
        print("\n✅ 就绪！按 Ctrl+C 退出")
        if not args.no_browser:
            webbrowser.open(f"http://localhost:{args.port}")
    threading.Thread(target=delayed_open, daemon=True).start()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n再见！")
        server.shutdown()

if __name__ == "__main__":
    main()
