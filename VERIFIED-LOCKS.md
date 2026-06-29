# FollowBox Verified Locks

> Purpose: record decisions that have been validated or are safety-critical, so later AI agents do not casually rewrite working solutions.

## How To Use

- Read this file before changing firmware, wiring, OTA, cloud H5, deployment, or AI skill files.
- If a task must touch a locked area, write `解锁理由` in the plan and in the top `AI-HANDOFF-MEMORY.md` entry.
- Run `python tools/check_verified_locks.py` after file changes.
- In CI or a clean handoff run, use `python tools/check_verified_locks.py --strict`.

## Locks

<!-- lock:BOARD_N32R16V paths=firmware/platformio.ini,firmware/boards/**,firmware/diagnostics/reset_probe/platformio.ini,firmware/include/config/ota_config.h severity=hard -->
### BOARD_N32R16V

- Locked decision: main board is `ESP32-S3-DevKitC-1-N32R16V` / `ESP32-S3-WROOM-2-N32R16V`, 32 MB Octal Flash + 16 MB Octal PSRAM, OPI, 1.8 V.
- Do not change board identity, flash mode, PSRAM mode, partition size, or fallback wording to another board without explicit user approval.
- Required validation if touched: PlatformIO build for main firmware and reset probe.

<!-- lock:PIN_MAP_GPIO paths=PIN-MAP-V1.md,CURRENT-WIRING-AI.md,FIRMWARE-SPEC.md,firmware/include/config/board_pins.h severity=hard -->
### PIN_MAP_GPIO

- Locked decision: GPIO constants live only in `firmware/include/config/board_pins.h`; old GPIO35/36/37/47/48 are forbidden for motor outputs.
- Do not move pin constants into business logic or restore old GPIO output plans.
- Required validation if touched: `rg` for forbidden GPIO use and firmware build.

<!-- lock:SAFETY_GATE paths=firmware/src/safety/**,firmware/src/app/command_pipeline.*,firmware/src/app/app.*,firmware/src/control/motion_mixer.*,firmware/src/drive/** severity=hard -->
### SAFETY_GATE

- Locked decision: all motion goes through `safety_manager -> applyFinalGate() -> drive_adapter`.
- Do not add alternate paths from H5, cloud, RC, sensor, or app code to motor output.
- Required validation if touched: logic smoke test if available plus PlatformIO build; safety-critical review required.

<!-- lock:PWM_OUTLET paths=firmware/src/drive/drive_adapter_analog_bldc.*,firmware/src/hal/pwm_output.*,firmware/include/config/board_pins.h severity=hard -->
### PWM_OUTLET

- Locked decision: `drive_adapter_analog_bldc` is the only PWM outlet for throttle output.
- Do not create another motor PWM writer or expose PWM control to H5/cloud.
- Required validation if touched: build plus bench-safe verification plan before hardware output.

<!-- lock:UWB_PROTOCOL paths=protocols/UWB-GC-P2304.md,firmware/src/sensors/uwb_gc_p2304.* severity=hard -->
### UWB_PROTOCOL

- Locked decision: UWB parser behavior must be based on captured/official protocol evidence.
- Do not invent parser fields, binary signed angle behavior, or undocumented frame layouts.
- Required validation if touched: protocol evidence link or captured frame sample plus parser test/build.

<!-- lock:OTA_PACKAGE paths=OTA-UPDATE-SPEC.md,tools/package_ota.py,firmware/include/config/ota_config.h,cloud/firmware/manifest.json,cloud/firmware/firmware.bin severity=medium -->
### OTA_PACKAGE

- Locked decision: device-affecting firmware/H5/profile/protocol edits require version bump and OTA package generation; pure cloud/docs edits must state why no device OTA is needed.
- Do not publish a manifest whose MD5/size does not match `firmware.bin`.
- Required validation if touched: `python tools/package_ota.py` or explicit no-OTA reason.

<!-- lock:CLOUD_H5_DEPLOY paths=cloud/**,devspace.yaml,DEVSPACE-AI-WORKFLOW.md,k8s/followbox-cloud.yaml,tools/followbox-control-center.ps1 severity=hard -->
### CLOUD_H5_DEPLOY

- Locked decision: cloud H5 deployment is isolated to the FollowBox cloud service and must not write parent directories or other cloud projects.
- Cloud service source is `cloud/`; cloud H5 source is `cloud/public/`; cloud OTA artifacts live in `cloud/firmware/`.
- Kubernetes namespace defaults to `followbox-dev`; DevSpace deployment name is `followbox-cloud`.
- Remote writes must target only a FollowBox cloud root, normally ending in `followbox-cloud`.
- Do not run broad `scp`, `rsync`, `rm`, PM2 restart-all, or copy commands against `/www/wwwroot`, `/www/server`, home directories, or multi-project folders.
- Required validation if touched: `node --check cloud/server.js cloud/public/app.js` when JS changed, plus `/api/health` or `devspace run cloud-check` for deployment work.

<!-- lock:AI_SKILLS_HANDOFF paths=AGENTS.md,AI-AGENT-RUNBOOK.md,skills/**,.agents/skills/**,tools/check_ai_handoff.py,tools/check_verified_locks.py,VERIFIED-LOCKS.md severity=medium -->
### AI_SKILLS_HANDOFF

- Locked decision: FollowBox AI work uses repo-local instructions, `.agents/skills` wrappers, `skills/` detailed project skills, `AI-HANDOFF-MEMORY.md`, and local gate scripts.
- Do not remove handoff requirements, skill routing, or check scripts to make a task appear complete.
- Required validation if touched: skill validation and `python tools/check_ai_handoff.py`.

## Current Strictness

The lock checker defaults to warning mode because this repository often has a dirty worktree from several AI agents. Use strict mode when starting from a clean handoff point or in CI.
