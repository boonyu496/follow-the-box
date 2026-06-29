---
name: followbox-plan-first
description: Use before changing FollowBox files, especially when work touches firmware, safety, GPIO, power, OTA, H5/cloud deploy, protocols, or more than one module. Requires a scoped plan, affected files, risk level, verification, and rollback/stop conditions before edits.
---

# FollowBox Plan First

## Required Plan

Before editing, produce a compact plan with:

- `task_id`
- `stage`: architecture, coding, review, debug, bench, calibration, docs, deploy, or test
- `risk_level`: low, medium, high, or safety-critical
- files to read
- files to edit, capped at 3 unless the user approves a wider task
- verified locks that may be touched
- cloud/H5 deployment boundary, if relevant
- validation commands
- blocking conditions

## Stop Conditions

Stop and ask or return `BLOCKED` when:

- The plan would modify a `VERIFIED-LOCKS.md` item without a clear reason.
- A safety-critical change lacks test evidence or a safe bench procedure.
- A cloud deploy command would target a path broader than the FollowBox cloud root.
- The task would mix firmware safety changes with cloud/tooling refactors.
- UWB parser behavior would be invented without captured protocol evidence.

## Completion

After edits:

1. Run the closest validation command.
2. Run `python tools/check_ai_handoff.py`.
3. Run `python tools/check_verified_locks.py`.
4. Record any skipped validation explicitly in `AI-HANDOFF-MEMORY.md`.
