# FollowBox OTA 发布与安装规范

> 状态：当前实现的正式发布流程。适用于 `https://www.boonai.cn/fb/`、`cloud/server.js` 与 `firmware/src/ota/cloud_ota_manager.*`。
>
> 核心原则：发布、检查、安装严格分离。准备或部署新版本不等于授权设备安装。

## 1. 三个必须一致的发布物

| 项目 | 权威位置 | 要求 |
|---|---|---|
| 固件内嵌版本 | `firmware/include/config/ota_config.h` 的 `FOLLOWBOX_FIRMWARE_VERSION` | 必须大于设备当前版本 |
| 云端版本与校验 | `cloud/firmware/manifest.json` | `version`、`size`、`md5` 必须对应新二进制 |
| OTA 应用固件 | `cloud/firmware/firmware.bin` | 必须来自同一次成功构建 |

只更新其中一项都不构成有效发布：

- 固件版本未递增：H5 显示“已是最新版本”。
- manifest 与二进制不匹配：服务器把固件判为未发布。
- 只部署 H5 静态文件：不会产生新的 OTA 版本。
- 只通过 Git 部署：`.gitignore` 忽略 `*.bin`，新 `firmware.bin` 不会随 commit/pull 到服务器。

## 2. 当前能力边界

- 云端 H5 只提供版本检查和显式安装授权，当前没有“上传/发布固件”页面。
- 云端发布目录是 `/www/wwwroot/followbox-cloud/firmware/`，其中必须同时存在匹配的 `manifest.json` 和 `firmware.bin`。
- 当前设备 OTA 使用 `U_FLASH`，只更新应用固件分区。
- `firmware/web/` 属于 LittleFS。修改本地车端 H5 后，仅发布 `firmware.bin` 不会更新 LittleFS；需另行构建并通过受控方式部署文件系统镜像。
- `cloud/public/` 属于云端 H5，修改后应按云端静态文件部署流程发布，不需要设备 OTA。

## 3. 发布前安全条件

1. 车辆保持急停，驱动禁止，确认不会产生实体运动。
2. 使用稳定供电，升级期间不得断电。
3. 保留 USB 恢复路径；首次修改分区表不能通过普通 OTA 完成。
4. 不得把“检查更新”实现为自动安装；只有用户明确点击安装后才能创建安装请求。

## 4. 标准发布流程

以下命令从项目根目录执行，示例使用 PowerShell。

### 4.1 递增版本

修改：

```cpp
// firmware/include/config/ota_config.h
#define FOLLOWBOX_FIRMWARE_VERSION "<NEW_VERSION>"
```

同一命名序列中递增最后一个数字最稳妥，例如：

```text
2026.06.19-ota-h5.4 -> 2026.06.19-ota-h5.5
```

服务器使用分词比较版本；新版本必须严格大于当前运行版本，不能复用旧版本号覆盖二进制。

### 4.2 构建应用固件

```powershell
$env:PLATFORMIO_CORE_DIR = (Resolve-Path 'firmware\.pio-core').Path
$env:PLATFORMIO_HOME_DIR = $env:PLATFORMIO_CORE_DIR
pio run -d firmware
```

只有输出 `[SUCCESS]` 才能继续。目标产物为：

```text
firmware/.pio/build/esp32-s3-devkitc-1/firmware.bin
```

### 4.3 同步云端发布二进制

```powershell
Copy-Item -LiteralPath `
  'firmware\.pio\build\esp32-s3-devkitc-1\firmware.bin' `
  -Destination 'cloud\firmware\firmware.bin' -Force

$fw = Get-Item 'cloud\firmware\firmware.bin'
$md5 = (Get-FileHash -Algorithm MD5 $fw.FullName).Hash.ToLowerInvariant()
$fw.Length
$md5
```

### 4.4 更新 manifest

将上一步得到的实际值写入 `cloud/firmware/manifest.json`：

```json
{
  "version": "<NEW_VERSION>",
  "file": "firmware.bin",
  "md5": "<ACTUAL_MD5>",
  "size": 0,
  "force": false,
  "notes": "本版本的用户可读变更摘要"
}
```

`size` 必须替换为实际字节数。正常发布保持 `force: false`；当前安全模型仍要求用户授权安装。

## 5. 发布包一致性校验

```powershell
node --check cloud\server.js

@'
const fs = require("fs");
const crypto = require("crypto");
const config = fs.readFileSync("firmware/include/config/ota_config.h", "utf8");
const version = (config.match(/FOLLOWBOX_FIRMWARE_VERSION\s+"([^"]+)"/) || [])[1];
const manifest = JSON.parse(fs.readFileSync("cloud/firmware/manifest.json", "utf8"));
const binary = fs.readFileSync(`cloud/firmware/${manifest.file}`);
const md5 = crypto.createHash("md5").update(binary).digest("hex");
const ok = !!version && version === manifest.version &&
  manifest.size === binary.length && manifest.md5.toLowerCase() === md5;
console.log({ok, version, size: binary.length, md5});
if (!ok) process.exit(1);
'@ | node

$version = (Select-String 'FOLLOWBOX_FIRMWARE_VERSION\s+"([^"]+)"' `
  'firmware\include\config\ota_config.h').Matches[0].Groups[1].Value
rg -a -F $version cloud\firmware\firmware.bin

git diff --check
```

全部检查通过后才能部署。不得手工沿用上一个版本的 MD5 或大小。

## 6. 部署到云服务器

将以下两个文件同时上传到：

```text
/www/wwwroot/followbox-cloud/firmware/manifest.json
/www/wwwroot/followbox-cloud/firmware/firmware.bin
```

可使用宝塔面板、完整部署包或 SCP。账号、端口、私钥从当前受控部署环境取得，不在仓库中保存凭据。

若同时修改了 `cloud/server.js` 或 `cloud/public/`，再执行服务器端：

```bash
cd /www/wwwroot/followbox-cloud
bash deploy-clean-cache.sh
```

只发布固件文件且服务端程序未变化时，`server.js` 会在请求时重新读取 manifest 和二进制，通常无需重启；仍需完成下一节验证。

## 7. H5 与设备验收

1. 打开 `https://www.boonai.cn/fb/`，进入 OTA 区域并点击“检查更新”。
2. OTA 前应显示：
   - 当前版本：设备正在运行的旧版本；
   - 可用版本：刚发布的新版本；
   - 状态：发现新版本，等待用户选择。
3. 若当前版本与可用版本相同，先确认是否忘记递增固件内嵌版本。
4. 若显示“未发布”，检查远端二进制是否仍是旧文件，以及远端 manifest 的 MD5/size 是否匹配。
5. 用户明确授权后才能点击安装。安装期间持续保持安全停车和稳定供电。
6. 安装成功并重启、设备重新上报后，当前版本才应变为新版本。
7. 最后检查设备日志中的 OTA 结果，并确认应用功能、传感器有效性与安全门控没有回归。

## 8. 失败处理

| 现象 | 优先检查 |
|---|---|
| H5 显示已是最新 | 固件版本是否递增；manifest 是否仍是旧版本 |
| H5 显示未发布 | 远端文件是否存在；MD5 和大小是否匹配 |
| 下载失败 | URL、鉴权、反向代理、远端文件权限 |
| MD5/长度失败 | 是否混用了两次构建的 manifest 与二进制 |
| 安装后版本不变 | 设备是否实际重启并重新上报；OTA result 原因 |
| 本地车端 H5 没变化 | 该内容在 LittleFS，不属于当前应用固件 OTA |

任何失败都不能通过绕过校验、复用旧版本号或自动强制安装来处理。
