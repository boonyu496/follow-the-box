#!/usr/bin/env python3
"""FollowBox verified-lock gate.

Default mode reports dirty files that touch verified locks but exits 0 so it can
run in an already-dirty multi-agent worktree. Use --strict in CI or after a clean
handoff to fail on locked-file edits without a top handoff lock note.
"""
from __future__ import annotations

import argparse
import fnmatch
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOCKS_FILE = ROOT / "VERIFIED-LOCKS.md"
HANDOFF = ROOT / "AI-HANDOFF-MEMORY.md"
REQUIRED_LOCK_IDS = {
    "BOARD_N32R16V",
    "PIN_MAP_GPIO",
    "SAFETY_GATE",
    "PWM_OUTLET",
    "UWB_PROTOCOL",
    "OTA_PACKAGE",
    "CLOUD_H5_DEPLOY",
    "AI_SKILLS_HANDOFF",
}
LOCK_RE = re.compile(
    r"<!--\s*lock:([A-Z0-9_-]+)\s+paths=([^>]+?)\s+severity=([a-z-]+)\s*-->",
    re.IGNORECASE,
)


@dataclass(frozen=True)
class Lock:
    lock_id: str
    paths: tuple[str, ...]
    severity: str


def run(cmd: list[str]) -> tuple[int, str]:
    try:
        p = subprocess.run(
            cmd,
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=10,
        )
        return p.returncode, p.stdout
    except Exception as exc:
        return 999, str(exc)


def parse_locks() -> tuple[list[Lock], list[str]]:
    issues: list[str] = []
    if not LOCKS_FILE.exists():
        return [], ["missing VERIFIED-LOCKS.md"]
    text = LOCKS_FILE.read_text(encoding="utf-8", errors="replace")
    locks: list[Lock] = []
    for match in LOCK_RE.finditer(text):
        lock_id = match.group(1).upper()
        paths = tuple(p.strip().replace("\\", "/") for p in match.group(2).split(",") if p.strip())
        severity = match.group(3).lower()
        locks.append(Lock(lock_id=lock_id, paths=paths, severity=severity))
    found = {lock.lock_id for lock in locks}
    for lock_id in sorted(REQUIRED_LOCK_IDS - found):
        issues.append(f"missing lock marker: {lock_id}")
    return locks, issues


def changed_paths() -> tuple[list[str], list[str]]:
    code, inside = run(["git", "rev-parse", "--is-inside-work-tree"])
    if code != 0 or inside.strip() != "true":
        return [], []
    code, status = run(["git", "status", "--short"])
    if code != 0:
        return [], [f"git status failed: {status.strip()}"]
    paths: list[str] = []
    for line in status.splitlines():
        if not line.strip():
            continue
        raw = line[3:].strip()
        if " -> " in raw:
            raw = raw.split(" -> ", 1)[1].strip()
        paths.append(raw.replace("\\", "/").strip('"'))
    return paths, []


def matches(path: str, pattern: str) -> bool:
    normalized = path.replace("\\", "/").rstrip("/")
    pat = pattern.rstrip("/")
    return fnmatch.fnmatch(normalized, pat) or fnmatch.fnmatch(normalized + "/", pat.rstrip("/") + "/**")


def handoff_has_lock_note() -> bool:
    if not HANDOFF.exists():
        return False
    text = HANDOFF.read_text(encoding="utf-8", errors="replace")
    marker = "## 最新交接记录"
    if marker in text:
        text = text.split(marker, 1)[1]
    next_record = text.split("\n### ", 2)
    top = next_record[1] if len(next_record) > 1 else text
    return "锁定影响" in top or "解锁理由" in top


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--strict", action="store_true", help="fail when locked files changed without handoff lock note")
    args = parser.parse_args()

    locks, issues = parse_locks()
    changed, change_issues = changed_paths()
    issues.extend(change_issues)

    touched: list[tuple[str, str, str]] = []
    for path in changed:
        for lock in locks:
            if any(matches(path, pattern) for pattern in lock.paths):
                touched.append((lock.lock_id, lock.severity, path))

    if issues:
        print("VERIFIED_LOCKS_CHECK=FAIL")
        for issue in issues:
            print("- " + issue)
        return 1

    if not touched:
        print("VERIFIED_LOCKS_CHECK=PASS")
        print("No changed files match verified locks.")
        return 0

    print("VERIFIED_LOCKS_CHECK=WARN")
    for lock_id, severity, path in touched:
        print(f"- {lock_id} ({severity}): {path}")

    if args.strict and not handoff_has_lock_note():
        print("\nStrict mode failure: locked files changed but top handoff entry lacks 锁定影响/解锁理由.")
        return 1

    if args.strict:
        print("\nStrict mode allowed: top handoff entry contains a lock note.")
    else:
        print("\nWarning mode: add 锁定影响/解锁理由 to the handoff when these edits are intentional.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
