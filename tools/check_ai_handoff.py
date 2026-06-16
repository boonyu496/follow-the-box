#!/usr/bin/env python3
"""
FollowBox AI handoff gate.

Run after any AI/Codex/Claude/Copilot task that may have changed files.
It verifies that AI-HANDOFF-MEMORY.md exists and, when git status is available,
that the handoff memory is part of the current changed-file set.
"""
from __future__ import annotations

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HANDOFF = ROOT / "AI-HANDOFF-MEMORY.md"
REQUIRED_FIELDS = ["改动", "文件", "架构影响", "安全影响", "验证", "当前状态", "下一步"]


def run(cmd: list[str]) -> tuple[int, str]:
    try:
        p = subprocess.run(cmd, cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10)
        return p.returncode, p.stdout
    except Exception as exc:
        return 999, str(exc)


def main() -> int:
    issues: list[str] = []

    if not HANDOFF.exists():
        issues.append("missing AI-HANDOFF-MEMORY.md")
    else:
        text = HANDOFF.read_text(encoding="utf-8", errors="replace")
        if "## 最新交接记录" not in text:
            issues.append("AI-HANDOFF-MEMORY.md missing section: ## 最新交接记录")
        for field in REQUIRED_FIELDS:
            if field not in text:
                issues.append(f"AI-HANDOFF-MEMORY.md missing required field word: {field}")

    code, inside = run(["git", "rev-parse", "--is-inside-work-tree"])
    if code == 0 and inside.strip() == "true":
        code, status = run(["git", "status", "--short"])
        if code == 0:
            changed = [line[3:].strip() for line in status.splitlines() if line.strip()]
            changed_norm = {p.replace("\\", "/") for p in changed}
            non_handoff_changes = [p for p in changed_norm if p not in {"AI-HANDOFF-MEMORY.md", "AI-HANDOFF-MEMORY.html"}]
            if non_handoff_changes and "AI-HANDOFF-MEMORY.md" not in changed_norm:
                issues.append(
                    "git has changed files but AI-HANDOFF-MEMORY.md is not changed. "
                    "Add a short handoff record before finishing."
                )
        else:
            issues.append("git status failed: " + status.strip())
    else:
        # Project may not be a git repo yet; static checks only.
        pass

    if issues:
        print("AI_HANDOFF_CHECK=FAIL")
        for issue in issues:
            print("- " + issue)
        print("\nFix: update AI-HANDOFF-MEMORY.md with an 8-12 line record under ## 最新交接记录.")
        return 1

    print("AI_HANDOFF_CHECK=PASS")
    print("AI-HANDOFF-MEMORY.md exists and contains required handoff fields.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
