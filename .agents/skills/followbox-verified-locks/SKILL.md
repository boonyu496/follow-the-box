---
name: followbox-verified-locks
description: Use whenever FollowBox work may touch already verified or safety-critical decisions, including board identity, pin map, PWM outlet, safety gate, OTA packaging, cloud H5 deploy paths, device IDs, secrets, or validated hardware assumptions. Prevents accidental AI rewrites of working solutions.
---

# FollowBox Verified Locks

## Workflow

1. Read `VERIFIED-LOCKS.md`.
2. Compare the intended edit files against the locked areas.
3. If a locked item is touched, include an explicit `解锁理由` in the plan and handoff record.
4. Run:

```bash
python tools/check_verified_locks.py
```

Use strict mode when the worktree is clean or in CI:

```bash
python tools/check_verified_locks.py --strict
```

## Rules

- Do not alter locked board/flash/PSRAM settings without explicit user instruction.
- Do not introduce another PWM outlet; `drive_adapter_analog_bldc` remains the only PWM output path.
- Do not bypass `safety_manager -> applyFinalGate() -> drive_adapter`.
- Do not deploy cloud H5 by copying a parent directory that contains other projects.
- Do not move secrets into tracked files.
- Do not shrink or delete historical handoff records to hide a lock change.

## Handoff

When touching a locked item, add a handoff line:

```text
- 锁定影响：触及 <lock id>；解锁理由：<why>; 验证：<evidence>
```

If no lock is touched:

```text
- 锁定影响：无
```
