---
name: followbox-diff-review
description: Use after FollowBox file changes or when reviewing another AI's work. Reviews only the current git diff and related locked areas first, focusing on regressions, safety risks, missing validation, handoff quality, and cloud deployment pollution risk.
---

# FollowBox Diff Review

## Workflow

1. Run `git status --short`.
2. Inspect only changed files first with `git diff -- <path>`.
3. Cross-check changed files against `VERIFIED-LOCKS.md`.
4. If firmware or safety files changed, route to `skills/07-code-review-debugger/SKILL.md` and, when relevant, `skills/03-safety-control-reviewer/SKILL.md`.
5. If cloud H5 or deploy tooling changed, route to `followbox-cloud-h5-deploy`.
6. Verify that `AI-HANDOFF-MEMORY.md` has a new top entry.

## Findings Priority

Report findings in this order:

- Safety regression or bypass.
- Verified lock changed without reason.
- Cloud deploy may overwrite another project.
- Build/test/OTA validation missing.
- Interface or protocol drift.
- Token-wasting context duplication.

## Commands

```bash
git status --short
python tools/check_ai_handoff.py
python tools/check_verified_locks.py
```

Use `--strict` for `check_verified_locks.py` only when pre-existing dirty files are not expected.
