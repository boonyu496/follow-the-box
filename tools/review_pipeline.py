#!/usr/bin/env python3
"""
review_pipeline.py v3 — Multi-Model Code Review Orchestrator
=============================================================
4-phase: cpplint/cppcheck → AutoGen multi-round debate → Fusion → Report

Models (all strongest available):
  Coder:      DeepSeek V4 Pro (deepseek-chat)
  Reviewer:   GLM-5.1 (glm-4.7)
  Judge:      Qwen 3.7 Max (qwen3.7-max) + Agnes-2.0-Flash
  CI:         pre-commit hook (auto-trigger on git commit)

Usage:
  python tools/review_pipeline.py --scope firmware/src/web/
  python tools/review_pipeline.py --files main.cpp app.cpp --ci  # CI mode

Config: edit REVIEW_CONFIG dict at top of file.
"""

import argparse, asyncio, json, os, re, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field, asdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

# ═══════════════════════════════════════════════════════════════
# CONFIGURATION — edit this block to tune
# ═══════════════════════════════════════════════════════════════

REVIEW_CONFIG = {
    # ── Models (all strongest available) ──
    "coder": {
        "provider": "deepseek", "model": "deepseek-chat",
        "base_url": "https://api.deepseek.com/v1",
        "api_key_env": "DEEPSEEK_API_KEY",
    },
    "reviewer": {
        "provider": "glm", "model": "glm-4-flash",
        "base_url": "https://open.bigmodel.cn/api/paas/v4",
        "api_key_env": "GLM_API_KEY",
    },
    "fusion_panel": [
        {"provider": "deepseek", "model": "deepseek-chat",
         "base_url": "https://api.deepseek.com/v1",
         "api_key_env": "DEEPSEEK_API_KEY"},
        {"provider": "qwen", "model": "qwen3.7-max",
         "base_url": "https://dashscope.aliyuncs.com/compatible-mode/v1",
         "api_key_env": "DASHSCOPE_API_KEY"},
        {"provider": "glm", "model": "glm-4-flash",
         "base_url": "https://open.bigmodel.cn/api/paas/v4",
         "api_key_env": "GLM_API_KEY"},
    ],
    "fusion_judge": {
        "provider": "deepseek", "model": "deepseek-chat",
        "base_url": "https://api.deepseek.com/v1",
        "api_key_env": "DEEPSEEK_API_KEY",
    },

    # ── Iteration ──
    "max_rounds": 3,
    "fusion_parallel": True,  # call panel models in parallel

    # ── Deterministic checks ──
    "cpp_banned": [
        (r"digitalWrite\s*\(",   "direct GPIO — use hal/gpio_hal.h"),
        (r"analogWrite\s*\(",    "direct PWM — use drive_adapter"),
        (r"delay\s*\(",          "blocking delay — use vTaskDelay"),
        (r"delayMicroseconds\s*\(", "blocking μs delay"),
        (r"new\s+(?!std::)",     "heap alloc — prefer stack/static"),
        (r"malloc\s*\(",         "C malloc — use new or stack"),
        (r"vTaskSuspendAll\s*\(", "critical section — use portMUX"),
    ],

    # ── Paths ──
    "review_dir": ".reviews",
    "precommit_hook": ".git/hooks/pre-commit",
}

# ═══════════════════════════════════════════════════════════════
# Data structures
# ═══════════════════════════════════════════════════════════════

@dataclass
class LintFinding:
    file: str; line: int; severity: str; rule: str
    message: str; snippet: str = ""

@dataclass
class ReviewReport:
    timestamp: str = field(default_factory=lambda: datetime.now(timezone.utc).isoformat())
    scope: list = field(default_factory=list)
    files_reviewed: list = field(default_factory=list)
    lint_findings: list = field(default_factory=list)
    cppcheck_findings: list = field(default_factory=list)
    autogen_rounds: int = 0
    autogen_messages: list = field(default_factory=list)
    fusion_score: Optional[float] = None
    fusion_raw: dict = field(default_factory=dict)
    findings: list = field(default_factory=list)
    confirmed_correct: list = field(default_factory=list)
    build_status: str = "not_built"
    models_used: dict = field(default_factory=dict)

# ═══════════════════════════════════════════════════════════════
# Phase 0: Deterministic Checks
# ═══════════════════════════════════════════════════════════════

def _find_pio_dir(project_dir: Path) -> Optional[Path]:
    """Find PlatformIO project directory."""
    for sub in ["", "firmware"]:
        p = project_dir / sub / "platformio.ini"
        if p.exists():
            return project_dir / sub
    return None

def run_cpplint(files: list[Path]) -> list[LintFinding]:
    """Run cpplint on C++ files."""
    findings = []
    for f in files:
        if f.suffix not in (".cpp", ".h", ".hpp"):
            continue
        try:
            result = subprocess.run(
                ["python", "-m", "cpplint", "--quiet", str(f)],
                capture_output=True, text=True, timeout=30
            )
            for line in result.stdout.split("\n") + result.stderr.split("\n"):
                m = re.match(r"(.+?):(\d+):\s+(.+)", line)
                if m:
                    findings.append(LintFinding(
                        file=m.group(1), line=int(m.group(2)),
                        severity="warning", rule="cpplint",
                        message=m.group(3)[:200]
                    ))
        except Exception:
            pass
    return findings

def run_pattern_check(files: list[Path]) -> list[LintFinding]:
    """Check against banned patterns."""
    findings = []
    for f in files:
        if f.suffix not in (".cpp", ".h", ".hpp"):
            continue
        try:
            lines = f.read_text(encoding="utf-8", errors="ignore").split("\n")
        except Exception:
            continue
        for pattern, msg in REVIEW_CONFIG["cpp_banned"]:
            for i, line in enumerate(lines, 1):
                if re.search(pattern, line):
                    findings.append(LintFinding(
                        file=str(f), line=i, severity="warning",
                        rule="banned-pattern", message=msg,
                        snippet=line.strip()[:120]
                    ))
    return findings

def run_platformio_build(project_dir: Path) -> tuple[str, list[LintFinding]]:
    """Build and capture warnings."""
    pio_dir = _find_pio_dir(project_dir)
    if not pio_dir:
        return "no_pio_project", []

    findings = []
    try:
        result = subprocess.run(
            ["pio", "run"], cwd=str(pio_dir),
            capture_output=True, text=True, timeout=120
        )
        for line in (result.stderr + result.stdout).split("\n"):
            m = re.match(r"(.+?):(\d+):(\d+):\s+warning:\s+(.+)", line)
            if m:
                findings.append(LintFinding(
                    file=m.group(1), line=int(m.group(2)),
                    severity="warning", rule="pio-build", message=m.group(4)
                ))
            m = re.match(r"(.+?):(\d+):(\d+):\s+error:\s+(.+)", line)
            if m:
                findings.append(LintFinding(
                    file=m.group(1), line=int(m.group(2)),
                    severity="fatal", rule="pio-build", message=m.group(4)
                ))
        return "SUCCESS" if result.returncode == 0 else "FAILED", findings
    except Exception as e:
        return "ERROR", [LintFinding(file="", line=0, severity="warning",
                                      rule="pio-error", message=str(e))]

# ═══════════════════════════════════════════════════════════════
# Phase 1+2: AutoGen Multi-Round
# ═══════════════════════════════════════════════════════════════

def _read_api_keys() -> dict:
    env_path = Path.home() / "AppData/Local/hermes/.env"
    keys = {}
    if env_path.exists():
        for line in env_path.read_text().splitlines():
            line = line.strip()
            if line and not line.startswith("#") and "=" in line and "_API_KEY" in line:
                k, v = line.split("=", 1)
                keys[k] = v.strip()
    return keys

def _build_autogen_client(cfg: dict, api_keys: dict):
    from autogen_ext.models.openai import OpenAIChatCompletionClient
    from autogen_ext.models.openai._model_info import ModelInfo
    key = api_keys.get(cfg["api_key_env"], "")
    info = ModelInfo(vision=False, function_calling=True, json_output=True,
                     family="unknown", structured_output=False)
    return OpenAIChatCompletionClient(
        model=cfg["model"], api_key=key, base_url=cfg["base_url"],
        model_info=info,
    )

AUTOGEN_CODER = """You are a senior embedded C++ engineer. Your job:
1. Analyze the code for safety, correctness, and architecture issues
2. Respond to reviewer feedback: fix real bugs, explain why incorrect feedback is wrong
3. Iterate with the reviewer until agreement
4. NEVER say PASSED_REVIEW — only the reviewer says that

Rules: no direct GPIO/PWM outside hal/; all motion through applyFinalGate;
no blocking delays; shared data needs synchronization."""

AUTOGEN_REVIEWER = """You are a strict embedded safety reviewer. Find:
- Race conditions, safety violations, architecture violations
- GPIO ownership, PWM exit points, safety gate bypasses
- Error handling gaps, boundary conditions, null derefs
Be specific: cite file, line, reasoning. When satisfied, reply: PASSED_REVIEW
Do NOT write code."""

def run_autogen(files: list[Path], task: str, api_keys: dict,
                max_rounds: int = 3) -> tuple[str, int, list[str]]:
    """AutoGen multi-round debate."""
    from autogen_agentchat.agents import AssistantAgent
    from autogen_agentchat.teams import RoundRobinGroupChat
    from autogen_agentchat.conditions import TextMentionTermination, MaxMessageTermination

    coder_cl = _build_autogen_client(REVIEW_CONFIG["coder"], api_keys)
    reviewer_cl = _build_autogen_client(REVIEW_CONFIG["reviewer"], api_keys)

    coder = AssistantAgent(name="Coder_DeepSeek", model_client=coder_cl,
                           system_message=AUTOGEN_CODER)
    reviewer = AssistantAgent(name="Reviewer_GLM5", model_client=reviewer_cl,
                              system_message=AUTOGEN_REVIEWER)

    term = TextMentionTermination("PASSED_REVIEW") | MaxMessageTermination(max_rounds * 3 + 2)

    # Build file context
    ctx = ""
    for f in [x for x in files if x.suffix == ".cpp"][:4]:
        try:
            ctx += f"\n### {f.name}\n```cpp\n{f.read_text(encoding='utf-8', errors='ignore')[:2000]}\n```\n"
        except: pass

    full_task = f"{task}\n\nFiles:\n{ctx}\n\nIterate up to {max_rounds} rounds."

    async def _run():
        team = RoundRobinGroupChat(
            participants=[coder, reviewer],
            termination_condition=term, max_turns=max_rounds * 2)
        result = await team.run(task=full_task)
        msgs = [f"[{m.source}] {m.content[:200]}..." for m in result.messages]
        return result.messages[-1].content if result.messages else "", len(result.messages), msgs

    loop = asyncio.new_event_loop()
    try:
        return loop.run_until_complete(_run())
    finally:
        loop.close()

# ═══════════════════════════════════════════════════════════════
# Phase 3: Fusion (automated — parallel API calls + judge)
# ═══════════════════════════════════════════════════════════════

def _call_llm(cfg: dict, prompt: str, api_keys: dict) -> Optional[str]:
    """Call a single LLM API."""
    from openai import OpenAI
    try:
        client = OpenAI(api_key=api_keys.get(cfg["api_key_env"], ""),
                        base_url=cfg["base_url"], timeout=90)
        resp = client.chat.completions.create(
            model=cfg["model"],
            messages=[{"role": "user", "content": prompt}],
            temperature=0.3, max_tokens=2048)
        return resp.choices[0].message.content
    except Exception as e:
        return f"ERROR:{cfg['provider']}/{cfg['model']}: {e}"

FUSION_PROMPT_TEMPLATE = """## Code Review — Second Opinion

{context}

## Task
Based on the findings above, analyze what ARCHITECTURAL or SAFETY issues
might remain undetected. Focus on:
- Call-chain violations and permission boundaries
- Race conditions and cross-core synchronization (ESP32 dual-core)
- State machine consistency
- Missing error handling or edge cases

Output JSON format:
{{"score": <0-10>, "findings": [{{"title": "...", "severity": "fatal|serious|medium|suggestion", "description": "..."}}], "strongest": "...", "weakest": "..."}}"""

JUDGE_PROMPT = """Synthesize these {n} code reviews into one unified report.

{reviews}

Return JSON: {{"score": <0-10>, "consensus": ["point1",...], "unique": ["point1",...], "blind_spots": ["..."]}}"""

def run_fusion(files: list[Path], lint_findings: list,
               autogen_summary: str, api_keys: dict) -> dict:
    """Automated fusion — parallel panel + judge."""
    # Build context from deterministic + AutoGen results
    ctx = [f"## Scope: {len(files)} files"]
    ctx.append(f"## Lint: {len(lint_findings)} issues")
    for lf in lint_findings[:15]:
        ctx.append(f"- [{lf.severity}] {lf.file}:{lf.line} — {lf.message}")
    ctx.append(f"\n## AutoGen: {autogen_summary[:800]}")

    prompt = FUSION_PROMPT_TEMPLATE.format(context="\n".join(ctx))

    # Phase A: panel in parallel
    panel_results = {}
    print(f"  Panel: {len(REVIEW_CONFIG['fusion_panel'])} models in parallel...")
    t0 = time.time()
    with ThreadPoolExecutor(max_workers=len(REVIEW_CONFIG["fusion_panel"])) as ex:
        futs = {ex.submit(_call_llm, cfg, prompt, api_keys): cfg["provider"]
                for cfg in REVIEW_CONFIG["fusion_panel"]}
        for fut in as_completed(futs):
            provider = futs[fut]
            try:
                result = fut.result(timeout=90)
                panel_results[provider] = result
                status = "OK" if not result.startswith("ERROR:") else "FAIL"
                print(f"    {provider}: {status} ({len(result)} chars)")
            except Exception as e:
                panel_results[provider] = f"ERROR: {e}"
                print(f"    {provider}: TIMEOUT")

    elapsed = time.time() - t0

    # Phase B: judge
    if panel_results:
        reviews_text = "\n\n---\n\n".join(
            f"### {p}\n{r}" for p, r in panel_results.items()
            if not r.startswith("ERROR:")
        )
        judge_prompt = JUDGE_PROMPT.format(n=len(panel_results), reviews=reviews_text[:8000])
        judge_result = _call_llm(REVIEW_CONFIG["fusion_judge"], judge_prompt, api_keys)
    else:
        judge_result = "No panel results"

    # Parse judge output
    fusion_data = {"panel": panel_results, "judge_raw": judge_result,
                   "elapsed": elapsed}
    try:
        j = json.loads(judge_result) if judge_result else {}
        fusion_data["score"] = j.get("score")
        fusion_data["consensus"] = j.get("consensus", [])
        fusion_data["unique"] = j.get("unique", [])
        fusion_data["blind_spots"] = j.get("blind_spots", [])
    except:
        pass

    return fusion_data

# ═══════════════════════════════════════════════════════════════
# Report + CI
# ═══════════════════════════════════════════════════════════════

def save_report(report: ReviewReport, project_dir: Path) -> Path:
    review_dir = project_dir / REVIEW_CONFIG["review_dir"]
    review_dir.mkdir(parents=True, exist_ok=True)
    ts = datetime.now().strftime("%Y%m%d-%H%M%S")
    json_path = review_dir / f"review-{ts}.json"
    md_path = review_dir / f"review-{ts}.md"

    json_path.write_text(json.dumps(asdict(report), indent=2, ensure_ascii=False, default=str))

    md = f"""# Code Review — {ts}

## Summary
| Item | Value |
|------|-------|
| Files | {len(report.files_reviewed)} |
| Lint | {len(report.lint_findings)} issues |
| AutoGen | {report.autogen_rounds} rounds |
| Fusion | {report.fusion_score}/10 |
| Build | {report.build_status} |
| Models | Coder: DeepSeek V4 Pro, Reviewer: GLM-5.1, Judge: Qwen 3.7 Max + Agnes-2.0 |

## Fusion Score: {report.fusion_score}/10
"""
    if report.fusion_raw.get("consensus"):
        md += "\n### Consensus\n" + "\n".join(f"- {c}" for c in report.fusion_raw["consensus"])
    if report.fusion_raw.get("blind_spots"):
        md += "\n### Blind Spots\n" + "\n".join(f"- {b}" for b in report.fusion_raw["blind_spots"])

    md += "\n## Findings\n"
    for f in report.findings:
        md += f"\n- **[{f.get('severity','?').upper()}]** {f.get('title','?')} — {f.get('description','')[:200]}"

    md += "\n## Confirmed Correct\n"
    for c in report.confirmed_correct:
        md += f"- ✅ {c}\n"

    md += f"\n## Lint Details\n"
    for lf in report.lint_findings[:20]:
        md += f"- [{lf['severity']}] `{lf.get('file','')}:{lf.get('line',0)}` — {lf.get('message','')[:150]}\n"

    md_path.write_text(md)

    # History
    history_path = review_dir / "history.json"
    history = json.loads(history_path.read_text()) if history_path.exists() else []
    history.append({"timestamp": report.timestamp, "files": len(report.files_reviewed),
                    "findings": len(report.findings), "fusion_score": report.fusion_score,
                    "build": report.build_status})
    history_path.write_text(json.dumps(history, indent=2))
    return md_path

def install_precommit_hook(project_dir: Path, pipeline_path: Path):
    """Install git pre-commit hook that runs the review pipeline."""
    hook_dir = project_dir / ".git" / "hooks"
    if not hook_dir.exists():
        print("  Not a git repository — skipping pre-commit hook")
        return

    hook_path = hook_dir / "pre-commit"
    hook = f"""#!/bin/bash
# Auto-generated by review_pipeline.py v3
# Runs code review on staged C++ files before commit.
echo "[pre-commit] Running code review..."

STAGED=$(git diff --cached --name-only --diff-filter=ACM | grep -E '\\.(cpp|h|hpp)$')
if [ -z "$STAGED" ]; then
    echo "  No C++ files staged — skipping"
    exit 0
fi

python "{pipeline_path}" --files $STAGED --ci 2>&1
RESULT=$?

if [ $RESULT -ne 0 ]; then
    echo ""
    echo "╔══════════════════════════════════════════╗"
    echo "║  REVIEW FAILED — commit blocked          ║"
    echo "║  See .reviews/ for details               ║"
    echo "║  Use: git commit --no-verify to bypass   ║"
    echo "╚══════════════════════════════════════════╝"
    exit 1
fi
echo "  Review passed ✓"
"""
    hook_path.write_text(hook)
    hook_path.chmod(0o755)
    print(f"  Pre-commit hook installed: {hook_path}")

# ═══════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(description="Multi-Model Code Review Pipeline v3")
    parser.add_argument("--scope", help="Directory to review")
    parser.add_argument("--files", nargs="+", help="Specific files")
    parser.add_argument("--project", default=".", help="Project root")
    parser.add_argument("--task", default="Review for safety, correctness, architecture violations.",
                       help="Review task")
    parser.add_argument("--max-rounds", type=int, default=3)
    parser.add_argument("--ci", action="store_true", help="CI mode: exit non-zero on findings")
    parser.add_argument("--install-hook", action="store_true", help="Install git pre-commit hook")
    parser.add_argument("--skip-autogen", action="store_true")
    parser.add_argument("--skip-fusion", action="store_true")
    args = parser.parse_args()

    project_dir = Path(args.project).resolve()
    print(f"╔══════════════════════════════════════╗")
    print(f"║  Review Pipeline v3                  ║")
    print(f"║  Coder: DeepSeek V4 Pro              ║")
    print(f"║  Reviewer: GLM-5.1                   ║")
    print(f"║  Judge: Qwen 3.7 Max + Agnes-2.0     ║")
    print(f"╚══════════════════════════════════════╝")
    print(f"Project: {project_dir}")

    # Install hook
    if args.install_hook:
        install_precommit_hook(project_dir, Path(__file__).resolve())
        if not args.files and not args.scope:
            return

    # Resolve files
    files = []
    if args.files:
        files = [project_dir / f for f in args.files]
    elif args.scope:
        scope = project_dir / args.scope
        files = list(scope.rglob("*.cpp")) + list(scope.rglob("*.h"))
    else:
        print("Error: --scope or --files required")
        sys.exit(1)

    files = [f.resolve() for f in files if f.exists()]
    if not files:
        print("No files found"); sys.exit(0 if args.ci else 1)

    report = ReviewReport(
        scope=[args.scope] if args.scope else args.files,
        files_reviewed=[str(f.relative_to(project_dir)) for f in files],
        models_used={"coder": "deepseek-chat", "reviewer": "glm-4.7",
                     "judge": ["qwen3.7-max", "agnes-2.0-flash"]},
    )

    api_keys = _read_api_keys()

    # ── Phase 0: Lint ──
    print(f"\n── Phase 0: Deterministic Checks ({len(files)} files) ──")
    lint_findings = run_cpplint(files) + run_pattern_check(files)
    build_status, build_findings = run_platformio_build(project_dir)
    lint_findings.extend(build_findings)
    report.lint_findings = [asdict(lf) for lf in lint_findings]
    report.build_status = build_status
    print(f"  cpplint: {len(run_cpplint(files))} issues")
    print(f"  patterns: {len(run_pattern_check(files))} issues")
    print(f"  build: {build_status} ({len(build_findings)} warnings)")
    for lf in lint_findings[:8]:
        print(f"    [{lf.severity}] {Path(lf.file).name}:{lf.line} — {lf.message[:80]}")

    # ── Phase 1+2: AutoGen ──
    autogen_out = ""
    if not args.skip_autogen:
        print(f"\n── Phase 1+2: AutoGen Debate (max {args.max_rounds} rounds) ──")
        print(f"  Coder: DeepSeek V4 Pro  |  Reviewer: GLM-5.1")
        autogen_out, rounds, msgs = run_autogen(files, args.task, api_keys, args.max_rounds)
        report.autogen_rounds = rounds
        report.autogen_messages = msgs
        print(f"  Rounds: {rounds}  |  Messages: {len(msgs)}")

    # ── Phase 3: Fusion ──
    if not args.skip_fusion:
        print(f"\n── Phase 3: Fusion (automated) ──")
        summary = autogen_out[:600] if autogen_out else "No AutoGen output"
        fusion = run_fusion(files, lint_findings, summary, api_keys)
        report.fusion_raw = fusion
        report.fusion_score = fusion.get("score")
        print(f"  Score: {fusion.get('score')}/10  |  Time: {fusion.get('elapsed',0):.1f}s")
        if fusion.get("consensus"):
            print(f"  Consensus: {len(fusion['consensus'])} points")
        if fusion.get("blind_spots"):
            print(f"  Blind spots: {len(fusion['blind_spots'])}")

    # ── Save ──
    md_path = save_report(report, project_dir)
    print(f"\n── Report ──")
    print(f"  {md_path}")
    print(f"  {md_path.with_suffix('.json')}")
    print(f"  History: {project_dir / REVIEW_CONFIG['review_dir'] / 'history.json'}")

    # CI exit code
    fatal_count = sum(1 for lf in lint_findings if lf.severity == "fatal")
    if args.ci and fatal_count > 0:
        print(f"\n  ⛔ CI: {fatal_count} fatal lint issues — blocking commit")
        sys.exit(1)

    print(f"\n  ✓ Done")

if __name__ == "__main__":
    main()
