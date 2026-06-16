# FollowBox 远程调试与 OTA 方案 — 完整审查报告

> 审查日期：2026-06-12  
> 审查人：Hermes Agent  
> 审查范围：跨网络烧录（OTA）可行性、H5 云端方案可行性、待办任务落地路径

---

## 1. 原始需求与结论一览

| 你的问题 | 结论 | 证据 |
|---------|------|------|
| ESP32-S3 跨网络 OTA 烧录是否可行？ | ✅ **完全可行**，首烧必须 USB | ESP32-S3 原生支持 HTTP(S) OTA；项目已有 8MB Flash 分区空间 |
| H5 端存到云服务器上是否可行？ | ✅ **可行，已实现 80%** | `cloud/server.js` + `cloud/public/` 完整实现，只差部署 |
| 通过 H5 云端控制小车 | ✅ **可行，已实现** | `cloud_client.cpp` 已实现 150ms 命令轮询 + deadman 安全 |
| WiFi 连上后传日志到云端 | ✅ **可行，已实现** | `cloud_client.cpp` 已实现 1s 一次 telemetry POST + 日志上传 |
| 这样能方便调试和烧录吗？ | ✅ **三合一**：H5 面板 + OTA + 日志 = 远程开发闭环 | 打通后不需要每次搬车到开阔地 |

---

## 2. 方案 A：云端远程调试（零硬件成本）

### 2.1 整体架构

```
                         ┌──────────────────────────────┐
                         │       云服务器 (Node.js)       │
                         │   https://www.boonai.cn/fb/    │
                         │                                │
                         │  REST API:                     │
                         │   /api/device/{id}/ingest      │  ← 遥测上传
                         │   /api/device/{id}/command     │  → 远程点动
                         │   /api/device/{id}/events      │  → SSE 推送
                         │   /api/device/{id}/firmware/   │  ← OTA 版本检查
                         │                                │
                         │  静态文件:                      │
                         │    / (index.html, app.js)      │  ← H5 控制面板
                         └──────────┬───────────────────┘
                                    │ HTTPS / 公网
                                    │
              ┌─────────────────────┼─────────────────────┐
              │                     │                     │
        ┌─────┴─────┐      ┌──────┴──────┐     ┌──────┴──────┐
        │ FollowBox  │      │  浏览器     │     │  开发者     │
        │ ESP32-S3   │      │  H5 面板    │     │  curl/CLI   │
        │            │      │            │     │             │
        │ WiFi STA ──┼──────► 实时状态   │     │ 上传固件    │
        │ 遥测 1s    │      │ SSE/WS     │     │ 查询日志    │
        │ OTA检查    │      │ 远程点动   │     │ 远程调试    │
        └────────────┘      └─────────────┘     └─────────────┘
```

### 2.2 已有代码清单（可直接复用）

| 文件 | 功能 | 状态 |
|------|------|------|
| `firmware/src/cloud/cloud_client.cpp` | ESP32 遥测上传 + 命令轮询 | ✅ 已实现 |
| `firmware/include/config/cloud_config.h` | 云端 URL/token 配置 | ⚠️ 需填真实值 |
| `firmware/include/config/network_config.h` | WiFi STA/AP 模式切换 | ⚠️ 需填 WiFi 密码 |
| `cloud/server.js` | 云端 Node.js 服务器 | ✅ 已实现 |
| `cloud/public/index.html` | 云端 H5 控制面板 | ✅ 已实现 |
| `CLOUD-TELEMETRY-SPEC.md` | 云端安全边界规范 | ✅ 已定义 |

### 2.3 部署步骤（约 30 分钟）

1. **服务器端部署**（云服务器 boonai.cn）
   ```bash
   mkdir -p /www/wwwroot/followbox-cloud/cloud
   scp -r /mnt/d/car/Follow\ the\ box/cloud/* root@82.156.85.60:/www/wwwroot/followbox-cloud/
   cd /www/wwwroot/followbox-cloud
   npm install
   pm2 start server.js --name followbox-cloud
   ```

2. **Nginx 配置** — 反向代理到 `https://www.boonai.cn/fb/`

3. **ESP32 端配置**
   - 修改 `cloud_config.h`：填入 API_BASE_URL、DEVICE_TOKEN
   - 修改 `network_config.h`：填入 STA_SSID、STA_PASSWORD，`USE_SOFT_AP = false`

4. **编译烧录**（Windows 侧）
   ```powershell
   cd D:\car\Follow the box\firmware
   pio run -t upload       # 烧录固件
   pio run -t uploadfs     # 上传 H5 静态文件
   ```

---

## 3. 方案 B：OTA 远程烧录（分区表改造）

### 3.1 核心结论

ESP32-S3 原生支持 OTA，但当前分区表 `default_8MB.csv` 不支持 OTA，需要：

1. 创建 OTA 分区表 `partitions/ota_8MB.csv`（app0 + app1 双分区 + coredump）
2. 增加 OTA 管理器 `src/ota/ota_manager.cpp`
3. 云端新增固件版本 API
4. OTA 中强制禁用电机输出

### 3.2 分区表变更

| 分区 | 当前 | 变更后 | 说明 |
|------|------|--------|------|
| app0 | ~2.9MB | ~3.2MB | OTA 当前运行分区 |
| app1 | 无 | ~3.2MB | OTA 目标分区 |
| littlefs | ~5MB | ~1.5MB | 缩小但够（当前仅 ~50KB） |
| coredump | 无 | 64KB | OTA 失败后可排查 |

### 3.3 风险矩阵

| 风险 | 概率 | 影响 | 缓解 |
|------|:---:|:---:|------|
| OTA 中断变砖 | 低 | 高 | 双分区 + rollback，变砖后 USB 可恢复 |
| WiFi 断线 | 中 | 中 | 失败不切换分区，保持旧固件 |
| 分区表变更擦除 NVS | 中 | 中 | 首次烧 OTA 分区表后需重新校准 |

---

## 4. 待办任务清单与落地路径

### 4.1 当前状态全景

| 模块 | 状态 | 备注 |
|------|:----:|------|
| 双核任务化 | ✅ | control(Core1) + sensor(Core0) + comm(Core0) |
| safety_manager | ✅ | 多层门控 |
| mode_manager | ✅ | 8 模式 |
| cloud_client | ✅ | 已实现遥测+命令 |
| cloud/server.js | ✅ | REST API + SSE |
| OTA 分区表 | ⬜ | 需新建 |
| OTA 管理器 | ⬜ | 需编写 |
| 云服务器部署 | ⬜ | 需部署 |
| 本地 H5 面板 | ✅ | firmware/data/ |

### 4.2 P0/P1 待办（优先完成）

| # | 任务 | 优先级 | 依赖 | 预估 |
|:-:|------|:------:|------|:----:|
| 1 | 部署云服务器到 `https://www.boonai.cn/fb/` | P0 | 无 | 30min |
| 2 | 填写 `cloud_config.h` 的 URL/ID/Token | P0 | 1 | 15min |
| 3 | 填写 `network_config.h` 的 WiFi STA | P0 | — | 15min |
| 4 | 合并 `firmware/web/` 与 `firmware/data/`（删 web/） | P1 | — | 15min |
| 5 | 本地 USB 首烧验证 + 云端遥测 | P0 | 2+3+4 | 1h |
| 6 | 创建 OTA 分区表 `ota_8MB.csv` | P1 | 5 完成 | 30min |
| 7 | 编写 OTA 管理器 `ota_manager.cpp` | P1 | 6 | 2h |

### 4.3 室内测试小底盘（可选）

如果原车太大不便室内测试，可另造小底盘：

| 项目 | 规格 | 预算 |
|------|------|------|
| 4WD 亚克力底盘 | 约 ¥50 | ¥50 |
| N20 微型减速电机×4 | ¥10×4 | ¥40 |
| 电机驱动 L298N/DRV8833 | — | ¥20 |
| 3S 18650 电池组 | — | ¥50 |
| **合计** | | **~¥180** |

小底盘 + 云端调试 = 桌面遥控测试 + OTA 验证，无差别。

---

## 5. 安全边界（严格执行）

### 5.1 云端禁止操作

| ❌ 禁止 | 原因 |
|---------|------|
| 云端直接设置 PWM/油门值 | 绕过本地安全链 |
| 云端清除急停状态 | 物理急停必须物理恢复 |
| 云端绕过安装向导 | 未完成安全确认前禁止 AUTO_FOLLOW |
| 云端绕过速度限制 | MANUAL_CLOUD_LOW_SPEED 最低速 |
| 云端远程解锁 FAULT_LOCKOUT | 必须本地人工确认 |

### 5.2 OTA 安全规则

1. OTA 中 `safety_manager` 强制 `enable=false, PWM=0, brake=true`
2. OTA 失败必须保持旧固件不动
3. 固件和文件系统分开更新
4. 版本号 + MD5 双重校验
5. OTA 完成后上报版本到云端

---

## 6. 建议落地顺序

```
本周（不需要硬件）              下周（需要 ESP32 硬件）        后续
┌──────────────────────┐      ┌────────────────────────┐   ┌──────────────────┐
│ 1. 部署云服务器       │      │ 4. 本地 USB 首烧验证    │   │ 7. 小底盘测试     │
│ 2. 写 OTA-SPEC       │ ──► │ 5. 云端遥测 + 点动     │ ─►│                  │
│ 3. 合并 H5 目录      │      │ 6. OTA 分区表 + 管理器  │   │ 8. 轨迹记录/回放  │
└──────────────────────┘      └────────────────────────┘   └──────────────────┘
```

---

## 7. 交付文件

| 文件 | 内容 |
|------|------|
| `test/00-审查结论汇总.md` | 总体评分 + 关键结论 + 问题分级 + 需求逐条对照 |
| `test/01-OTA可行性方案.md` | OTA 技术调研 + 分区表设计 + 库选择 + 安全规则 |
| `test/02-H5云端遥测控制方案.md` | 云服务器部署 + 网络拓扑 + 已有代码复用 |
| `test/03-待办任务清单与落地.md` | P0-P3 待办 + 优先级 + 依赖 + 预估 |
| `test/04-远程调试与OTA完整方案.md` | 本文件 — 综合汇总 |
