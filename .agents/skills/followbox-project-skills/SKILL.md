---
name: followbox-project-skills
description: Use for any FollowBox coding, review, debugging, documentation, H5/cloud deploy, firmware, wiring, calibration, or AI-handoff task. Loads the repo-local FollowBox skill package from skills/README.md, routes to the correct project skill, and enforces handoff, verified-lock, OTA, and cloud deployment guardrails.
---

# FollowBox Project Skills

## Workflow

1. Read `AGENTS.md`, `AI-HANDOFF-MEMORY.md`, and `skills/README.md` before task work.
2. For broad or risky work, read `VERIFIED-LOCKS.md` before choosing files to edit.
3. Route the task through the project-local skill package in `skills/`:
   - Unknown routing: `skills/00-dispatcher/SKILL.md`
   - Architecture or module boundaries: `skills/01-firmware-architecture-guardian/SKILL.md`
   - Implementation task brief: `skills/02-firmware-implementation-planner/SKILL.md`
   - Safety/motion/drive path: `skills/03-safety-control-reviewer/SKILL.md`
   - Sensors/protocols: `skills/04-sensor-protocol-integrator/SKILL.md`
   - Drive power/calibration: `skills/05-drive-power-calibration-engineer/SKILL.md`
   - H5/WebSocket/telemetry/cloud UI: `skills/06-h5-telemetry-ui-engineer/SKILL.md`
   - Diff/log/build review: `skills/07-code-review-debugger/SKILL.md`
   - Power-on, bench, or road testing: `skills/08-bringup-test-safety-officer/SKILL.md`
4. If the task edits files, update `AI-HANDOFF-MEMORY.md` and run:

```bash
python tools/check_ai_handoff.py
python tools/check_verified_locks.py
```

Use `python tools/check_verified_locks.py --strict` in CI or a clean handoff run.

## Hard Rules

- Do not bypass the project-local `skills/` package just because this wrapper triggered first.
- Do not change verified decisions in `VERIFIED-LOCKS.md` unless the user explicitly asks or the task cannot be completed otherwise.
- Do not modify cloud deployment paths, PM2 process names, Kubernetes namespaces, or remote roots without reading `followbox-cloud-h5-deploy`.
- Do not treat cloud H5 and embedded device H5 as the same artifact:
  - Cloud H5 lives under `cloud/public/`.
  - Embedded device H5 lives under `firmware/web/` and `firmware/data/`.
- Every file-changing task must leave a short top entry in `AI-HANDOFF-MEMORY.md`.
