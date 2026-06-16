# FollowBox 完整项目审查报告（上线就绪性评估）

> 审查日期：2026-06-12
> 审查范围：firmware/（main、网络、云端、OTA、Web、遥测、分区表、platformio.ini）、cloud/（server.js、H5面板、nginx）、首烧与二次OTA流程
> 结论：**不能直接上线**。存在 1 个架构性缺口（AP配网）和 4 个 P0 级代码缺陷，启用云端即崩溃、本地H5面板首烧后无法显示。

---

## 总体结论

| 需求项 | 现状 | 可上线 |
|---|---|:---:|
| AP配网（连热点→H5输入WiFi→连云端） | **完全未实现** | ❌ |
| H5 远程遥控（云端） | 80% 实现，但安全停/模式按钮在服务端被丢弃 | ❌ |
| H5 远程遥控（本地热点） | 已实现，但 LittleFS 分区标签错误导致面板挂载失败 | ❌ |
| OTA 二次烧录 | 仅局域网 ArduinoOTA（需连小车热点）；**云端远程OTA未实现** | ⚠️ |
| 日志采集调试 | 链路已通（环形缓冲→云端ingest→SSE→面板），但云上行有栈溢出 | ❌ |
| 首次USB烧录 | 流程与分区表正确 | ✅ |
| 本地安全链（急停/deadman/超时/限速） | 设计完整，质量高 | ✅ |

---

## P0 缺陷（必须修复才能上线）

### 1. AP 配网流程不存在（架构性缺口）
需求：用户连接小车热点 → 在H5页面输入家用/现场WiFi → 小车以STA连上外网 → 与云服务器通讯。

现状（`include/config/network_config.h`、`src/web/h5_web_server.cpp:80-87`）：
- WiFi 模式是**编译期二选一**（`FOLLOWBOX_WIFI_STA` 宏），默认纯 AP，无 AP+STA 共存。
- STA 凭据是编译期常量且为空字符串，无 NVS 存储、无 `/api/wifi` 配网接口、H5 页面无配网 UI。
- 直接后果：默认 AP 模式下 `WiFi.status() != WL_CONNECTED` 恒成立（`cloud_client.cpp:133`），**云端通讯在出厂配置下永远不工作**。

修复方案：`WiFi.mode(WIFI_AP_STA)`；新增 `POST /api/wifi`（SSID+密码）写 NVS（新建 `wifi_store`，复用 ProfileStore 模式）；启动时读 NVS 凭据尝试 STA，AP 常开兜底；H5 增加配网页 + STA 状态/IP 显示；连接失败可重配。

### 2. LittleFS 分区标签不匹配 → H5 面板挂载失败
`partitions/ota_8MB.csv` 分区名为 `littlefs`，但 `LittleFS.begin()`（h5_web_server.cpp:96）默认查找标签 `"spiffs"`（已核实本仓库 framework 头文件 `LittleFS.h:27` 默认参数）。`buildfs/uploadfs` 按 subtype 烧写会成功，但**运行时挂载失败**，面板静默不可用（代码视为非致命）。

修复（二选一）：分区表中将名字 `littlefs` 改为 `spiffs`；或 `LittleFS.begin(false, "/littlefs", 10, "littlefs")`。

### 3. 启用云端即栈溢出
`CloudClient::uploadTelemetry`（cloud_client.cpp:253-296）局部缓冲 ≈ 1280+2600+4200+288+160 ≈ **8.5 KB**，加上 `drainRecentJson` 内部 12×192 ≈ 2.3 KB 复制数组，全部在 comm 任务栈上 —— 而 comm 任务栈仅 **4096 字节**（main.cpp:179）。`FOLLOWBOX_CLOUD_ENABLED=1` 后第一次上传必触发 stack canary 崩溃重启。

修复：缓冲改 `static`（comm 任务单线程使用，安全）或堆分配；同时把 comm 任务栈加到 ≥8192 以容纳 HTTPClient/TLS。

### 4. 云端 H5 的"安全停"和模式按钮全部失效
`cloud/public/app.js` 发送 `{safe_idle:true}` 和 `{mode_request:...}`，但 `cloud/server.js` POST /command 只透传 `seq/deadman/forward/turn`，**丢弃 safe_idle 和 mode_request**；固件 `applyCommandBody` 解析的 `safe_idle` 永远收不到。云端"安全停"按钮无效（仅靠 750ms TTL 兜底停车），三个模式按钮纯摆设。

修复：server.js 透传 `safe_idle`；模式按钮要么删除、要么端到端实现（固件侧云端仅允许 SAFE_IDLE / MANUAL_CLOUD_LOW_SPEED，这个限制是对的，UI 应一致）。

### 5. 云端配置未就绪且两端 token 不一致
固件：`FOLLOWBOX_CLOUD_ENABLED=0`、`API_BASE_URL=""`、`DEVICE_TOKEN="CHANGE_ME"`；服务端默认 token 是另一串硬编码值。当前编译产物不可能连上已部署的 boonai.cn/fb 服务。需统一为部署时注入（build_flags / 环境变量），并随配网功能改为 NVS 可配置。

---

## P1 问题（上线前应解决）

6. **云端远程 OTA 未实现**。现有 OTA 仅 ArduinoOTA/espota（`[env:ota]` 固定 `192.168.4.1:3232`），必须人到现场连小车热点。需求中"H5 页面远程 OTA"需要：固件 HTTPS 拉取 OTA（esp_https_ota 或 esp32FOTA）+ 服务端固件版本接口 + H5 触发按钮 + `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` 回滚自检。`test/01-OTA可行性方案.md` 只是方案文档，未落码。分区表（双 3MB OTA 槽 + otadata + coredump，合计恰好 8MB）本身正确。
7. **服务端重启后命令全部失效**：server.js 的 seq 存内存，重启归零；固件 `last_seq` 只增不减（cloud_client.cpp:197），重启后所有命令因 `seq <= last_seq` 被丢弃，直到小车断电重启。修复：服务端持久化 seq，或固件检测 seq 大幅回退时重置。
8. **SSE /events 完全无鉴权**且向所有连接广播所有设备的状态/日志/命令（server.js broadcast 用全局 clients 集合，不按设备过滤）；GET /command 的设备 token 放 URL query 会进访问日志。
9. **HTTPS 实际不可用**：`HTTPClient::begin(url)` 对 https 无 CA/未 setInsecure 会握手失败；当前固件只能走明文 HTTP，遥控命令与 token 明文传输。需 `WiFiClientSecure` + CA 固定（或最低限度 setInsecure 过渡），nginx 侧强制 TLS。
10. **硬编码弱凭据**：AP 密码 `followbox123`、OTA 密码 `followbox-ota`、服务端默认双 token 均在源码中。工业产品应每台设备唯一（出厂写 NVS/efuse）。
11. **OTA 刷错配置即失联变砖**：若以 `FOLLOWBOX_WIFI_STA=1` 且 STA_SSID 为空的固件 OTA 刷入，设备既无 AP 也连不上 STA，只能拆机 USB 重刷。配网功能（P0-1）+ OTA 回滚自检（P1-6）可根治。

## P2 建议

12. ArduinoOTA 文件系统更新（U_SPIFFS）前未在 onStart 调 `LittleFS.end()`，有损坏风险。
13. 日志环仅 12 条 × 192 B、1 s 上传一次，故障瞬间高频日志会丢；建议加大环深或掉线时本地暂存（LittleFS）。
14. 云面板摄像头默认 `http://192.168.4.1:81/stream`，在公网页面不可达，HTTPS 下还有混合内容拦截；应走服务端转发或留空。
15. `g_ota_in_progress` 出错后永不清除（保持安全态是对的），但恢复需断电重启——写入运维手册即可。
16. `test/00-审查结论汇总.md` 称"OTA 代码未写、分区表未改"，与当前代码（已写 ArduinoOTA、已用 ota_8MB.csv）不符，文档需同步。
17. 命令通道 150 ms HTTP 短连接轮询开销大、延迟抖动明显，量产建议换 WebSocket/MQTT 长连接。
18. 按交接文档自述，**从未做过真机烧录、uploadfs 和 H5 联调**；上线前必须完成架空轮真机验证清单（FIRMWARE-FLASH-FIX-HANDOFF.md 阶段1）。
19. ESP32-S3-DevKitC 有 N8/N8R2/N16 等多种 flash 规格，分区表要求 **8MB flash**，采购/产测需锁定模块型号。

---

## 做得好的部分

双核任务划分清晰（Core1 仅控制/安全/PWM，Core0 承担阻塞 I/O）；安全链完整：estop → safety → mode → pipeline → mixer → applyFinalGate → drive 单出口，云端命令限 `MANUAL_CLOUD_LOW_SPEED` 且限速 0.08、deadman + seq 防重放 + 固件 700 ms / 服务端 750 ms 双重失联停车；OTA 期间控制环强制 `stopNow()`；H5 POST 命令不走 WS 入站、body 超长拒绝而非截断；JSON 全部定长缓冲无堆分配（除栈溢出问题外思路正确）；首烧流程文档（USB → 验证清单 → 再开 OTA）专业可执行。

## 修复优先级路线

1. 修 P0-2（一行改动）→ 真机首烧验证本地 H5 遥控（阶段1清单）。
2. 实现 AP+STA 配网（P0-1）+ 云端凭据 NVS 化（P0-5）。
3. 修栈溢出（P0-3）、server.js 透传 safe_idle（P0-4）、seq 重置（P1-7），打通云端遥测/遥控/日志。
4. 加 HTTPS（P1-9）、SSE 鉴权（P1-8）、唯一凭据（P1-10）。
5. 实现云端拉取式 OTA + 回滚自检（P1-6），替代/补充 espota。
6. 完成真机回归 + 架空轮安全验证后方可上线。
