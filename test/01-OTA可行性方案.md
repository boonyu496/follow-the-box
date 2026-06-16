# FollowBox ESP32-S3 OTA 远程烧录可行性方案

> 审查日期：2026-06-12
> 前提：首烧必须本地 USB，首烧通过后方可启用 OTA 通道

---

## 1. 核心结论：✅ 完全可行，但有硬约束

| 项目 | 结论 | 依据 |
|------|------|------|
| ESP32-S3 是否支持 OTA | ✅ 支持 | ESP32-S3 出厂 Bootloader 原生支持 OTA，Arduino/ESP-IDF 均有成熟 API |
| 当前 project 是否可加 OTA | ✅ 可加 | 已用 ESPAsyncWebServer，可复用；需改分区表和加 OTA 库 |
| 跨网络（公网）OTA | ✅ 可行 | HTTP(S) 下载 .bin 文件即可，无需局域网 |
| 首烧后可不拆机更新 | ✅ 是 | OTA 成功后重启即运行新固件 |
| 先有云服务器再 OTA | ✅ 推荐 | 已有 cloud/server.js，OTA 版本检查可复用同一通道 |
| 涉及安全是否允许 OTA | ⚠️ 必须限制 | OTA 中禁止运动，更新失败必须可 USB 恢复 |

---

## 2. 硬件要求

### 2.1 Flash 分区：必须改

当前使用 `board_build.partitions = default_8MB.csv`，该表只有 single factory app + littlefs：

```
# default_8MB.csv（当前 - 不支持 OTA）
nvs,        data, nvs,     0x9000,   0x5000,
otadata,    data, ota,     0xe000,   0x2000,
app0,       app,  ota_0,   0x10000,  0x2E0000,   # ~2.9MB
littlefs,   data, spiffs,  0x2F0000, 0x500000,   # ~5MB
```

**需要的 OTA 分区表（`partitions/ota_8MB.csv`）：**

```
# OTA 8MB for ESP32-S3 N8R8
nvs,        data, nvs,     0x9000,   0x5000,
otadata,    data, ota,     0xe000,   0x2000,
app0,       app,  ota_0,   0x10000,  0x330000,   # ~3.2MB
app1,       app,  ota_1,   0x340000, 0x330000,   # ~3.2MB
littlefs,   data, spiffs,  0x670000, 0x180000,   # ~1.5MB
coredump,   data, coredump,0x7F0000, 0x10000,
```

**注意：** 当前固件编译后约 2.1MB（Flash 26.8%），N8R8 有 8MB Flash。3.2MB/app 足够。但 LittleFS 从 5MB 缩小到 1.5MB，需确认 H5 面板 + 日志缓存是否足够。当前 `data/` 中仅 ~50KB，1.5MB 绰绰有余。

#### 分区表变更影响

| 项目 | 当前 default_8MB | 建议 ota_8MB | 影响 |
|------|-------------------|-------------|------|
| app0 | 2.9MB | 3.2MB | ✅ 更大 |
| app1 | 无 | 3.2MB | OTA 对偶分区 |
| littlefs | 5MB | 1.5MB | ⚠️ 减少但足够 |
| NVS | 同 | 同 | 无损 |

### 2.2 当前硬件兼容性

| 硬件 | 是否支持 OTA | 备注 |
|------|-------------|------|
| ESP32-S3-DevKitC-1 N8R8 | ✅ | 8MB Flash，WiFi 原生支持 |
| DS600 遥控 | N/A | OTA 不影响 RC 功能 |
| 电机控制器 | ⚠️ | OTA 中必须禁用电机输出 |
| 急停按钮 | ⚠️ | 物理急停始终有效，覆盖 OTA 状态 |

---

## 3. 现有代码库评审：OTA 依赖

当前 `pio.ini` 已含：

```ini
lib_deps =
  esp32async/ESPAsyncWebServer @ ^3.6.0   # 已有
  esp32async/AsyncTCP @ ^3.3.2            # 已有
```

### 3.1 OTA 方案对比

| 方案 | 原理 | 适用场景 | 复杂度 | 推荐度 |
|------|------|---------|--------|:-----:|
| **HTTP OTA (esp32FOTA)** | ESP32 主动检查云端版本 → 下载 .bin 写入 | **跨网络、规模部署** | 低 | ⭐⭐⭐ |
| AsyncElegantOTA | 浏览器上传 .bin 到 ESP32 | 局域网手动更新 | 最低 | ⭐⭐ |
| 原生 ESP HTTPS OTA | Update.writeStream + HTTPS | 自定义流程 | 中 | ⭐⭐⭐ |
| ArduinoOTA | Arduino IDE 网络端口 | 开发阶段局域网 | 低 | ⭐ |

### 3.2 推荐方案：esp32FOTA（即 esp32HTTPUpdateClient）

这是最适合 FollowBox 的方案：ESP32 主动轮询云服务器版本 → 下载新固件 → 校验 → 安装 → 重启。

**原理：**

```text
ESP32 固件
  │
  ├── 启动后连接 WiFi (STA)
  ├── 每 N 小时或收到服务器指令后
  │    └── HTTP GET {cloud_server}/api/device/{id}/firmware/version
  │         └── 返回 {"version":"1.2.0","url":"https://.../firmware_v1.2.0.bin","md5":"..."}
  │              └── 比较本地版号
  │                   ├── 相同 → 跳过
  │                   └── 不同 → HTTP GET .bin → Update.begin() → 写入分区
  │                              ├── 成功 → 重启
  │                              └── 失败 → 保持旧固件，上报错误
  └── 正常遥测循环
```

### 3.3 库选择

**推荐 esp32FOTA by chrisjoyce911**（PlatformIO: `chrisjoyce911/esp32FOTA`）：

```ini
lib_deps =
  chrisjoyce911/esp32FOTA @ ^2.1.0
```

核心 API：

```cpp
#include <esp32FOTA.h>

esp32FOTA fota;
fota.setURL("https://mycloud.com/api/device/followbox-001/firmware/version");
fota.checkAndUpdate();  // 自动检查版本并更新
```

备选：也可以自己实现轻量 HTTP OTA（无需额外库）：

```cpp
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

bool performOTA(const char* url) {
  HTTPClient http;
  http.begin(url);  // 可使用 HTTPS
  int code = http.GET();
  if (code != 200) return false;
  
  int total = http.getSize();
  WiFiClient* stream = http.getStreamPtr();
  
  Update.begin(total);
  size_t written = Update.writeStream(*stream);
  if (written == total && Update.end()) {
    ESP.restart();
  }
  return false;
}
```

---

## 4. 建议架构

### 4.1 整体数据流

```
                        ┌──────────────────────────────┐
                        │       云服务器 (Node.js)       │
                        │   cloud/server.js              │
                        │                                │
                        │  REST API:                     │
                        │   /api/device/{id}/ingest      │  ← 遥测上传
                        │   /api/device/{id}/command     │  → 远程点动
                        │   /api/device/{id}/events      │  → SSE 推送
                        │                                │
                        │  OTA API:                      │
                        │   /api/device/{id}/firmware/   │  ← 版本检查
                        │         version                 │  ← 下载 .bin
                        │         download                 │
                        │                                │
                        │  静态文件:                      │
                        │    / (index.html, app.js)      │  ← H5 控制面板
                        └──────────┬───────────────────┘
                                   │ HTTPS / 公网
                                   │
              ┌────────────────────┼────────────────────┐
              │                    │                    │
        ┌─────┴─────┐      ┌──────┴──────┐     ┌──────┴──────┐
        │ FollowBox  │      │  浏览器     │     │  开发者     │
        │ ESP32-S3   │      │  H5 面板    │     │  curl/CLI   │
        │            │      │            │     │             │
        │ WiFi STA ──┼──────► 实时状态   │     │ 上传固件    │
        │ 遥测 1s    │      │ SSE/WS     │     │ 查询日志    │
        │ OTA检查    │      │ 远程点动   │     │ 远程调试    │
        └────────────┘      └────────────┘     └─────────────┘
```

### 4.2 新增的文件

```
firmware/
  ├── include/
  │   └── config/
  │       └── ota_config.h            ← OTA 配置（版本号、检查间隔、URL）
  ├── src/
  │   └── ota/
  │       ├── ota_manager.h           ← OTA 管理器头文件
  │       └── ota_manager.cpp         ← OTA 管理器实现
  ├── partitions/
  │   └── ota_8MB.csv                ← OTA 分区表
  └── platformio.ini                  ← 改：分区表 + lib_deps
```

### 4.3 OTA 安全规则（硬约束）

1. **OTA 中禁止任何运动输出** — `safety_manager` 必须在 OTA 过程中强制 `enable=false, PWM=0, brake=true`
2. **OTA 失败必须保持安全态** — 不自动 fallback 到不良固件
3. **只能 OTA 已本地验证通过的固件/LittleFS 镜像**
4. **固件和文件系统分开更新** — 改 H5 时只需 uploadfs，不刷固件
5. **版本号 + MD5 双重校验**
6. **涉及安全门控/PWM/ADC/GPIO 的 OTA 包必须重新本地编译+烟测**
7. **OTA 完成后必须上报版本、构建时间、更新结果到云端**

---

## 5. 云服务器扩展

当前 `cloud/server.js` 已实现 ingest/command/events 端点。OTA 需要新增：

### 5.1 新增 API 端点

```http
### 获取固件版本信息
GET /api/device/{deviceId}/firmware/version?token=...
→ {
    "ok": true,
    "version": "1.2.0",
    "build_time": "2026-06-12T10:00:00Z",
    "md5": "a1b2c3d4e5f6...",
    "url": "/api/device/{deviceId}/firmware/download",
    "filesystem_version": "1.0",
    "filesystem_md5": "...",
    "filesystem_url": "..." ,
    "changelog": "- 修复 xxx\n- 新增 yyy"
  }

### 下载固件 .bin
GET /api/device/{deviceId}/firmware/download?token=...
→ firmware.bin (binary/octet-stream)

### 下载文件系统 .bin
GET /api/device/{deviceId}/firmware/filesystem?token=...
→ littlefs.bin (binary/octet-stream)

### ESP32 上报 OTA 结果
POST /api/device/{deviceId}/ota-result
{
  "token": "...",
  "version": "1.2.0",
  "success": true,
  "error": "",
  "previous_version": "1.1.0"
}
```

### 5.2 固件管理（开发流程）

```
开发者
  │
  ├── pio run -d firmware                          # 编译
  ├── pio run -d firmware -t buildfs               # 编译 LittleFS
  ├── 本地 USB 首烧验证
  ├── pio run -d firmware -t build                  # 生成 .pio/build/*/firmware.bin
  │
  └── CI/CD 脚本（可选）
       ├── 上传 firmware.bin 到云服务器
       ├── 上传 littlefs.bin 到云服务器
       └── 更新 version 接口的返回值
```

---

## 6. 实施步骤

| 步骤 | 任务 | 预计工时 | 风险等级 |
|:----:|------|:-------:|:--------:|
| 1 | 创建 `partitions/ota_8MB.csv` 分区表 | 0.5h | 低 |
| 2 | 创建 `include/config/ota_config.h`（版本号/A端） | 0.5h | 低 |
| 3 | 创建 `src/ota/ota_manager.h/.cpp`（HTTP OTA 逻辑） | 2h | 中 |
| 4 | 扩展 `main.cpp`：comm_task 中每秒检查 OTA 标志 | 1h | 中 |
| 5 | 扩展 `cloud/client.js`：新增 version/download 端点 | 1.5h | 低 |
| 6 | H5 面板加 OTA 状态/版本显示 | 1h | 低 |
| 7 | 本地首烧验证通过后 - 首次 OTA 测试 | 1h | 高 |
| 8 | 编写 `OTA-UPDATE-SPEC.md` 正式规范 | 1h | 低 |
| 9 | 编写云端固件部署流程 | 1h | 低 |

---

## 7. 风险与限制

| 风险 | 可能性 | 影响 | 缓解措施 |
|------|:-----:|:----:|---------|
| OTA 中断导致变砖 | 低 | 高 | 双分区 + rollback；变砖后 USB 可恢复 |
| WiFi 断线导致 OTA 失败 | 中 | 中 | 失败不切换分区，保持旧固件 |
| 分区表变更擦除 NVS | 中 | 中 | 首烧 OTA 分区表时 NVS 会被格式化，需重新校准；后续 OTA 不碰分区表 |
| LittleFS 缩小导致空间不足 | 低 | 中 | 当前 data/ 仅 ~50KB，1.5MB 绰绰有余 |
| OTA 固件时间戳回退 | 低 | 低 | 版本号单调递增 |

---

## 8. 参考资料

- [ESP32 OTA (Over-the-Air) Updates – Random Nerd Tutorials](https://randomnerdtutorials.com/esp32-ota-over-the-air-arduino)
- [ESP32 Cloud Based OTA Firmware Updates - PlatformIO Community](https://community.platformio.org/t/esp32-cloud-based-ota-firmware-updates-not-local-server-based/23565)
- [esp32FOTA library - PlatformIO Registry](https://registry.platformio.org/libraries/chrisjoyce911/esp32FOTA)
- [ESP32 Partition Table - Espressif docs](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/partition_table.html)
- [ESP32 OTA from URL triggered in code](https://community.platformio.org/t/esp32-ota-from-url-triggered-in-code/28001)
