# FollowBox AI 运行与交接门禁

> 目标：尽量确保 Codex / Claude / Copilot / 其他 AI 写完代码后，都会更新 `AI-HANDOFF-MEMORY.md`，避免下一个 AI 重复读大量上下文、浪费 token。

## 结论先说

`AI-HANDOFF-MEMORY.md` 本身不能强制所有 AI 使用。  
要提高遵守率，必须同时使用四层约束：

1. **启动 prompt 明确要求**：每次启动 Codex/Claude/Copilot 都把交接记忆写入任务要求。
2. **项目内技能规则**：`.agents/skills/` 是 Codex 自动发现包装层，`skills/README.md` 和每个 `skills/*/SKILL.md` 是详细项目技能。
3. **已验证锁定规则**：`VERIFIED-LOCKS.md` 记录已跑通/安全关键方案，触及锁定项必须写 `解锁理由`。
4. **结束后门禁检查**：运行 `python3 tools/check_ai_handoff.py` 和 `python3 tools/check_verified_locks.py`，检查本次改动后是否更新过 `AI-HANDOFF-MEMORY.md` 并提示锁定项影响。

## 所有 AI 启动前必须读取

每次启动任何代码 AI，任务 prompt 前面都加：

```text
你在 FollowBox 项目中工作。开始前必须读取：
1. README.md
2. AI-HANDOFF-MEMORY.md
3. VERIFIED-LOCKS.md
4. skills/README.md
5. 根据任务类型读取 `.agents/skills/*/SKILL.md` 包装和对应 `skills/*/SKILL.md`
6. FIRMWARE-SPEC.md、CURRENT-WIRING-AI.md、PIN-MAP-V1.md、profiles/example_bldc_analog_36v.yaml

只读问答可按 skills/README.md 的轻量读取规则执行；涉及固件、安全、GPIO、电源、上电、运动测试时必须读取全部权威文件。skills 中的样机事实只是摘要，若与 PIN-MAP-V1.md/FIRMWARE-SPEC.md/CURRENT-WIRING-AI.md 冲突，以权威文件为准并先报告冲突。

涉及云端 H5 / `cloud/` / DevSpace / SCP / PM2 / Kubernetes / OTA 发布时，必须遵守云端部署隔离：只部署 FollowBox 的 `cloud/`、`cloud/public/`、`cloud/firmware/`，目标目录必须是 FollowBox 云端根目录（通常末尾为 `followbox-cloud`），禁止覆盖 `/www/wwwroot`、`/www/server`、用户 home、仓库父目录或任何包含多个项目的目录；禁止 `pm2 restart all`；部署后必须验证 `/api/health` 或 `devspace run cloud-check`。

如果你修改了任何代码、架构、文档、Profile、协议、测试方案或技能文件，结束前必须在 AI-HANDOFF-MEMORY.md 的“## 最新交接记录”下方顶部追加 8-12 行短交接记录，包含：改动、文件、架构影响、安全影响、OTA、验证、当前状态、下一步。

如果触及 `VERIFIED-LOCKS.md` 中的锁定项，交接记录必须增加 `锁定影响：...；解锁理由：...`；未触及则写 `锁定影响：无`。

用户要求所有文件修改都要产出可通过 H5 页面安装的 OTA 烧录版。凡是修改会影响设备固件行为、配置、协议或本地车端 H5 的任务，收尾必须递增 `firmware/include/config/ota_config.h` 的 `FOLLOWBOX_FIRMWARE_VERSION`，运行 `python tools/package_ota.py --notes "<本次摘要>"`，并在交接记录写清 OTA 版本、`cloud/firmware/manifest.json` 的 size/MD5、`cloud/firmware/firmware.bin` 是否已生成。仅修改云端 H5/纯文档且不需要设备固件更新时，也必须在交接记录的 `OTA：` 行明确写“不需要设备 OTA”的原因。

禁止跳过交接记忆；如果没有验证，写“验证：未验证”，不能假装通过。
```

## Codex 推荐启动 prompt

```bash
codex exec --full-auto '
你在 FollowBox 项目中工作。开始前必须读取 README.md、AI-HANDOFF-MEMORY.md、VERIFIED-LOCKS.md、skills/README.md，并按任务类型读取 .agents/skills/*/SKILL.md 包装和对应 skills/*/SKILL.md。
必须遵守 FIRMWARE-SPEC.md、CURRENT-WIRING-AI.md、PIN-MAP-V1.md、profiles/example_bldc_analog_36v.yaml。
只读问答可按 skills/README.md 的轻量读取规则执行；涉及固件、安全、GPIO、电源、上电、运动测试时必须读取全部权威文件。skills 中的样机事实只是摘要，冲突时以权威文件为准并先报告冲突。
涉及云端 H5/cloud/DevSpace/SCP/PM2/Kubernetes/OTA 发布时，只能部署 FollowBox 的 cloud/、cloud/public/、cloud/firmware/，目标必须是 FollowBox 云端根目录（通常末尾 followbox-cloud），禁止覆盖 /www/wwwroot、/www/server、home、仓库父目录或多项目目录，禁止 pm2 restart all，部署后验证 /api/health 或 devspace run cloud-check。
如果修改任何文件，结束前必须更新 AI-HANDOFF-MEMORY.md：在“## 最新交接记录”下方顶部追加 8-12 行，写清改动、文件、架构影响、安全影响、OTA、验证、当前状态、下一步。
触及 VERIFIED-LOCKS.md 锁定项必须写 锁定影响 和 解锁理由；未触及写 锁定影响：无。
用户要求每次文件修改后都要准备 H5 页面可安装的 OTA 烧录版；涉及设备固件/本地车端 H5/协议/Profile 的改动必须递增 FOLLOWBOX_FIRMWARE_VERSION 并运行 python tools/package_ota.py，纯云端/纯文档改动也要在 OTA 行说明不需要设备 OTA 的原因。
完成后运行或说明验证结果，并确保 python3 tools/check_ai_handoff.py 与 python3 tools/check_verified_locks.py 通过或只产生已解释的 warning。

任务：<这里写具体任务>
'
```

## Claude Code / Copilot 推荐 prompt

```text
你在 FollowBox 项目中工作。先读 README.md、AI-HANDOFF-MEMORY.md、VERIFIED-LOCKS.md、skills/README.md，并按任务读取 .agents/skills/*/SKILL.md 包装和对应 skills/*/SKILL.md。
必须遵守 FIRMWARE-SPEC.md、CURRENT-WIRING-AI.md、PIN-MAP-V1.md、profiles/example_bldc_analog_36v.yaml。
只读问答可按 skills/README.md 的轻量读取规则执行；涉及固件、安全、GPIO、电源、上电、运动测试时必须读取全部权威文件。skills 中的样机事实只是摘要，冲突时以权威文件为准并先报告冲突。
涉及云端 H5/cloud/DevSpace/SCP/PM2/Kubernetes/OTA 发布时，只能部署 FollowBox 的 cloud/、cloud/public/、cloud/firmware/，目标必须是 FollowBox 云端根目录（通常末尾 followbox-cloud），禁止覆盖 /www/wwwroot、/www/server、home、仓库父目录或多项目目录，禁止 pm2 restart all，部署后验证 /api/health 或 devspace run cloud-check。
如果修改任何文件，结束前必须更新 AI-HANDOFF-MEMORY.md，在“## 最新交接记录”下方顶部追加 8-12 行短记录：改动、文件、架构影响、安全影响、OTA、验证、当前状态、下一步。
触及 VERIFIED-LOCKS.md 锁定项必须写 锁定影响 和 解锁理由；未触及写 锁定影响：无。
用户要求每次文件修改后都要准备 H5 页面可安装的 OTA 烧录版；涉及设备固件/本地车端 H5/协议/Profile 的改动必须递增 FOLLOWBOX_FIRMWARE_VERSION 并运行 python tools/package_ota.py，纯云端/纯文档改动也要在 OTA 行说明不需要设备 OTA 的原因。
不要贴大段代码，不要写密钥，不要假装验证。
完成后运行或说明：python3 tools/check_ai_handoff.py；python3 tools/check_verified_locks.py

任务：<这里写具体任务>
```

## Hermes 委托其他 AI 的规则

Hermes 调 Codex/Claude/Copilot 前，必须把上面的交接规则放进 prompt。  
Hermes 收到代码 AI 完成后，必须自己再运行：

```bash
python3 tools/check_ai_handoff.py
python3 tools/check_verified_locks.py
```

如果检查失败，不能宣布任务完成；要要求该 AI 补写，或由 Hermes 根据实际 diff 补写交接记录。

## 门禁脚本用途

`tools/check_ai_handoff.py` 检查：

- `AI-HANDOFF-MEMORY.md` 是否存在；
- 是否含推荐字段；
- 如果当前目录是 git 仓库且有文件变更，是否也修改了 `AI-HANDOFF-MEMORY.md`；
- 是否调用 `tools/check_verified_locks.py` 提示锁定项影响；
- 如果不是 git 仓库，做静态格式检查。

它不能证明 AI 写得“好”，但能防止最常见的“改完代码忘记写交接”。
