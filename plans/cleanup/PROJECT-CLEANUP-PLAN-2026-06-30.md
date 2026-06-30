# FollowBox Project Cleanup Plan - 2026-06-30

> Document type: temporary cleanup plan.
> Category: `plans/cleanup/`.
> Lifecycle: delete this file after the cleanup work is completed, superseded by a newer cleanup plan, or summarized into `README.md` / `CURRENT-PROJECT-ARCHITECTURE.md` / `AI-HANDOFF-MEMORY.md`.
> Delete safety: this file is planning-only. It is not a firmware, wiring, protocol, cloud deploy, OTA, or source-of-truth document.

## Review Result

The previous cleanup plan is mostly sound, with three corrections:

1. Do not rebuild the project framework. The project already has a usable architecture, local skill tree, verified locks, firmware/cloud/H5 boundaries, and handoff checks. The correct work is consolidation and cleanup.
2. Do not delete evidence directories blindly. `output/`, `v/`, and `zhiliao/` contain historical test artifacts, vendor tools, logs, screenshots, or videos referenced by handoff records. They need an archive/index pass before removal.
3. Treat credentials/default tokens as a separate cleanup phase. Token and password hardening touches cloud, local H5, and firmware config boundaries, so it should not be mixed with cache cleanup or `main.cpp` refactoring.

## Current Framework Summary

The active framework is:

```text
inputs/sensors
  -> SharedState
  -> App::tick()
  -> safety_manager.evaluate()
  -> mode_manager.selectMode()
  -> command_pipeline.buildIntent()
  -> obstacle_manager.apply()
  -> motion_mixer.mix()
  -> safety_manager.applyFinalGate()
  -> drive_adapter_analog_bldc.writeCommand()
```

The current skill routing is already present and should be kept:

| Area | Skill |
|---|---|
| Architecture and module boundaries | `skills/01-firmware-architecture-guardian` |
| Implementation briefs | `skills/02-firmware-implementation-planner` |
| Safety, modes, mixer, drive path | `skills/03-safety-control-reviewer` |
| Sensors and protocols | `skills/04-sensor-protocol-integrator` |
| PWM, ADC, power, calibration | `skills/05-drive-power-calibration-engineer` |
| H5, telemetry, UI | `skills/06-h5-telemetry-ui-engineer` |
| Review, debug, build logs | `skills/07-code-review-debugger` |
| Power-on and bench safety | `skills/08-bringup-test-safety-officer` |

## Files And Areas Not To Touch First

Do not touch these in the first cleanup pass:

- `firmware/include/config/board_pins.h`: locked GPIO source of truth.
- `firmware/platformio.ini`, `firmware/boards/**`, `firmware/include/config/ota_config.h`: board/OTA lock areas.
- `firmware/src/safety/**`, `firmware/src/control/**`, `firmware/src/drive/**`, `firmware/src/app/**`: safety and motion path.
- `cloud/**`, `devspace.yaml`, `k8s/**`, `tools/followbox-control-center.ps1`: cloud deploy lock area.
- `zhiliao/**`, `v/**`, `output/**`: evidence and historical artifacts until indexed.

## Phase A - Add Ownership Map

Status: recommended first.

Goal: make every major file/area easy to route to the right skill.

Suggested files to edit:

- `CURRENT-PROJECT-ARCHITECTURE.md`
- `README.md`
- `AI-HANDOFF-MEMORY.md`

Scope:

- Add a concise "file ownership and skill routing" table.
- Mark temporary/legacy paths explicitly.
- Keep source-of-truth order unchanged.

Validation:

```powershell
python tools\check_ai_handoff.py
python tools\check_verified_locks.py
git diff -- README.md CURRENT-PROJECT-ARCHITECTURE.md AI-HANDOFF-MEMORY.md
```

Risk: low.

## Phase B - Remove Local Rebuildable Caches

Status: safe after user approval.

Goal: reduce local disk noise without changing tracked source.

Candidate paths:

- `firmware/.pio-core/`
- `firmware/.pio/`
- `vision_cam/.pio/`
- `.codebuddy/db/`

Rules:

- Delete only ignored files/directories.
- Do not use `git rm`.
- Confirm `git status --short` remains clean afterward.

Validation:

```powershell
git status --short
git status --ignored --short
```

Risk: low. Rebuild time may increase after cache removal.

## Phase C - Resolve `firmware/data` Legacy H5 Copy

Status: needs a small dedicated task.

Finding:

- `firmware/platformio.ini` currently uses `data_dir = web`.
- `firmware/web/` is the active embedded H5 source.
- `firmware/data/` is tracked and has drifted from `firmware/web/`.

Recommended action:

1. Keep `firmware/web/` as the single embedded H5 source.
2. Replace `firmware/data/` with a small README-only tombstone or remove it from tracked source.
3. Update docs that still reference `firmware/data/` as active.

Do not do this silently because older handoff records and review docs mention `firmware/data/`.

Validation:

```powershell
node --check firmware\web\app.js
pio run -d firmware -e esp32-s3-devkitc-1 -t buildfs
python tools\check_ai_handoff.py
python tools\check_verified_locks.py
```

Risk: medium-low. It touches embedded H5 ownership but should not affect runtime if `data_dir = web` remains unchanged.

## Phase D - Credential And Default Token Cleanup

Status: separate security cleanup task.

Finding:

- Cloud and firmware defaults include development tokens/passwords.
- Browser code contains a default cloud operator token for video relay / cloud panel convenience.
- Some defaults are documented as "change before field use", but they are still easy to propagate.

Recommended action:

1. Replace public default operator/device tokens with explicit empty or development-only placeholders.
2. Require environment/build flags for real deployment credentials.
3. Keep local bench behavior documented.
4. Avoid writing real secrets into tracked files or handoff records.

Candidate files:

- `cloud/server.js`
- `cloud/public/app.js`
- `firmware/include/config/cloud_config.h`
- `firmware/include/config/network_config.h`
- `firmware/web/app.js`
- `CLOUD-TELEMETRY-SPEC.md`

Validation:

```powershell
node --check cloud\server.js
node --check cloud\public\app.js
node --check firmware\web\app.js
pio run -d firmware -e esp32-s3-devkitc-1
python tools\check_ai_handoff.py
python tools\check_verified_locks.py
```

Risk: medium. May affect cloud connectivity and local H5 convenience if done without deployment config.

## Phase E - Split `firmware/src/main.cpp`

Status: do after documentation cleanup.

Finding:

- `firmware/src/main.cpp` is about 205 lines and now owns task entry functions, OTA service setup, persistence wiring, and delayed H5 calibration/wizard execution.
- This exceeds the "entry only" architecture target.

Recommended action:

1. Move task runtime and OTA setup into an app runtime module.
2. Keep `main.cpp` as setup/loop entry only.
3. Keep `controlTaskEntry()` as the only place calling `drive.writeCommand()`.
4. Preserve `safety_manager.applyFinalGate()` before the drive adapter.

Candidate files:

- `firmware/src/main.cpp`
- new `firmware/src/app/runtime.*` or `firmware/src/app/scheduler.*`
- `AI-HANDOFF-MEMORY.md`

Validation:

```powershell
pio run -d firmware -e esp32-s3-devkitc-1
firmware\tools\logic_smoke_test
rg -n "writeCommand|applyFinalGate|PIN_LEFT_THROTTLE_PWM|PIN_RIGHT_THROTTLE_PWM" firmware\src firmware\include
python tools\check_ai_handoff.py
python tools\check_verified_locks.py
```

Risk: high. Touches task scheduling and safety/motion path; route through architecture and safety review.

## Phase F - Archive Or Prune Historical Artifacts

Status: do after indexing.

Finding:

- `output/` contains old deployment zips, Playwright screenshots, and SSE logs.
- `v/` contains source/converted video files.
- `zhiliao/` contains vendor PDFs/tools and duplicated lidar tool packages.
- Some of these are referenced from `AI-HANDOFF-MEMORY.md` and project docs.

Recommended action:

1. Create an artifact index listing what each directory proves.
2. Keep vendor PDFs/specs that are still cited by protocol docs.
3. Move stale generated zips/screenshots/logs to an external archive or delete only after references are no longer needed.
4. Add `.gitignore` rules for future generated outputs once tracked artifacts are resolved.

Validation:

```powershell
rg -n "output/|v/6-3|zhiliao/" README.md AI-HANDOFF-MEMORY.md protocols tools firmware cloud
git status --short
```

Risk: medium. Deleting evidence too early can hurt future debugging.

## Recommended Execution Order

1. Phase A: add ownership map.
2. Phase B: remove ignored local caches.
3. Phase C: resolve `firmware/data` legacy copy.
4. Phase D: credential/default token cleanup.
5. Phase E: split `main.cpp`.
6. Phase F: artifact archive/prune.

## Stop Conditions

Stop and ask before continuing if:

- A cleanup step touches `VERIFIED-LOCKS.md` hard-lock areas.
- A deletion candidate is referenced by current docs or latest handoff records.
- A change would mix firmware safety edits with cloud/deployment cleanup.
- A credential cleanup would break the active device/cloud workflow without replacement config.

## Document Deletion Note

This file can be deleted when:

- all phases are completed, or
- a newer cleanup plan replaces it, or
- the durable conclusions are moved into source-of-truth docs.

Deletion command, when appropriate:

```powershell
Remove-Item -LiteralPath "plans\cleanup\PROJECT-CLEANUP-PLAN-2026-06-30.md"
```
