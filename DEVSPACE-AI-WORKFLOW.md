# FollowBox DevSpace + AI Workflow Plan

## Purpose

This document is the working plan for reducing the current host/VM/Git/cloud/OTA handoff friction.

The target is not to add another large tool. The target is to make each layer own one clear job:

- Git keeps source history synchronized.
- PlatformIO builds and uploads firmware.
- DevSpace owns the cloud-control/H5 development and deployment loop.
- `tools_local/` becomes a thin local control panel over trusted commands.
- GPT/Codex coordinate planning, code changes, validation, and handoff records.

Official DevSpace repository: https://github.com/devspace-cloud/devspace

The repository currently redirects to `devspace-sh/devspace`; DevSpace 6.x uses `devspace.yaml` as the project workflow file.

## Current Pain

The current workflow crosses too many boundaries manually:

```text
host or VM
  -> git clone / pull / push
  -> local firmware edit
  -> OTA build
  -> copy firmware.bin and manifest.json
  -> upload H5/cloud files to server
  -> restart cloud service
  -> open cloud H5
  -> trigger OTA
  -> device downloads and installs
```

`tools_local/` was created to reduce this pain, but it now mixes Git sync, SCP upload, PM2 restart, firmware packaging, OTA actions, browser UI, and private machine configuration. That makes it hard to trust and hard to maintain.

## Target Workflow

The desired day-to-day loop is:

```text
GPT/agent plans the task and risks
  -> Codex edits local repo files
  -> Codex runs focused checks
  -> PlatformIO builds firmware artifacts when needed
  -> DevSpace updates cloud/H5/OTA service
  -> H5/cloud triggers OTA only after explicit operator action
  -> hardware validation remains local and safety-gated
```

For cloud/H5 development:

```bash
devspace run doctor
devspace dev
```

Expected behavior:

- Deploy `cloud/` into the configured Kubernetes namespace.
- Sync local `cloud/` changes into the container.
- Forward the service to `http://localhost:8080`.
- Stream logs.
- Restart the cloud container after uploads when needed.

Local prerequisites:

- Docker Desktop must be running.
- Docker Desktop Kubernetes must be enabled, or `KUBECONFIG` must point to an imported reachable cluster.
- `kubectl config current-context` must return the intended context before `devspace dev`.
- `devspace run doctor` is the preflight check for Docker daemon, kubectl, current context, and cluster reachability.
- The cloud container exposes `/api/health`; Docker and Kubernetes probes use the same endpoint.

Docker Desktop local Kubernetes path:

```powershell
# Docker Desktop -> Settings -> Kubernetes -> Enable Kubernetes -> Apply & Restart
kubectl config use-context docker-desktop
kubectl config current-context
devspace run doctor
```

External Kubernetes path:

```powershell
$env:KUBECONFIG = "D:\path\to\kubeconfig"
kubectl config get-contexts
kubectl config use-context <context-name>
kubectl cluster-info
devspace run doctor
```

For release-style checks:

```bash
devspace run-pipeline firmware-check
devspace run-pipeline vision-check
devspace run-pipeline ai-handoff-check
```

## Role Split

### GPT

- Plan and decompose work.
- Mark safety-critical changes.
- Prepare exact Codex handoff context.
- Review risk, module ownership, and acceptance criteria.
- Avoid direct hardware actions.

### Codex

- Edit files in the local repository.
- Run local commands and validation.
- Update `AI-HANDOFF-MEMORY.md` after any file change.
- Preserve FollowBox safety constraints from `AGENTS.md`.
- Use DevSpace/PlatformIO/Git as command backends instead of inventing parallel flows.

### DevSpace

- Own `cloud/` build/deploy/dev/log/port-forward/sync.
- Provide a repeatable command layer for cloud-control and H5 work.
- Replace ad hoc SCP/PM2 deployment for the cloud service where possible.
- Not replace firmware flashing, serial monitoring, or real hardware safety validation.

### PlatformIO

- Own firmware builds.
- Own serial upload and direct network OTA upload when explicitly requested.
- Produce `firmware.bin` artifacts for cloud OTA publication.

### tools_local

- Stay local and machine-specific.
- Act as a control surface, not a second deployment system.
- Call trusted commands such as `git`, `pio`, and `devspace`.
- Keep secrets and private host configuration out of tracked files.

## Hard Boundaries

- Firmware source of truth remains `firmware/`.
- Embedded H5 source of truth remains `firmware/web/`.
- Cloud telemetry/control relay remains `cloud/`.
- Cloud H5 source of truth remains `cloud/public/`; cloud OTA publication artifacts remain `cloud/firmware/`.
- Cloud H5 must not set PWM, clear safety locks, or bypass setup/install gates.
- All motion still goes through `safety_manager -> applyFinalGate() -> drive_adapter`.
- `drive_adapter_analog_bldc` remains the only PWM outlet.
- Hardware flashing, serial monitoring, and real motor tests stay local.
- Safety-critical work still follows `architect -> safety-reviewer -> tester -> safety-officer`.

## Cloud H5 Deployment Isolation

The cloud host may contain multiple projects. Treat FollowBox cloud deployment as a narrow, project-scoped operation:

- Deploy only the repository `cloud/` directory for the FollowBox cloud service.
- Deploy only `cloud/public/` for cloud H5 assets.
- Deploy only `cloud/firmware/` for FollowBox OTA artifacts.
- Do not copy or delete parent directories such as `/www/wwwroot`, `/www/server`, user home directories, repository parents, or any directory that contains multiple projects.
- Remote targets must be the FollowBox cloud root, normally ending in `followbox-cloud`.
- Restart only the FollowBox service/process/container; do not run `pm2 restart all`.
- Exclude `.env`, `.env.local`, PEM/key files, tokens, `node_modules/`, logs, caches, and unrelated project folders.
- After deployment, verify `/api/health`, `deploy-version.txt`, or `devspace run cloud-check`.

## Security And Account Hygiene

This workflow should reduce dependence on interactive account sessions across host and VM.

Preferred mechanisms:

- Git SSH keys or deploy keys.
- Kubernetes context or service account with scoped access.
- Environment variables or local ignored config for tokens.
- `.env.local` or OS-level environment variables for private secrets.
- No tokens, PEM contents, passwords, or private cloud credentials in tracked files.

Avoid designing the workflow around repeated browser logins or copying private credentials between host and VM.

## Migration Plan

### Phase 1: Stabilize Existing Workflow

Goal: make the current local control path safer before larger changes.

Tasks:

- Replace destructive `git reset --hard origin/master` pull behavior with a safe pull path.
- Prefer `git pull --ff-only` for normal sync.
- Refuse pull when local changes would be lost unless the operator explicitly stashes or commits.
- Show current branch, dirty files, ahead/behind state, firmware version, cloud manifest version, and cloud reachability.
- Keep `tools_local/` ignored and local-only.

Validation:

```bash
git status --short --branch
python tools/check_ai_handoff.py
```

### Phase 2: Move Cloud/H5 Work To DevSpace

Goal: stop treating cloud deployment as a custom SCP script.

Tasks:

- Add DevSpace commands for cloud status, cloud logs, cloud check, and cloud deploy.
- Make `tools_local/` call DevSpace for cloud operations.
- Keep direct SCP/PM2 commands only as a temporary legacy fallback.
- Document required local toolchain: DevSpace, Docker, kubectl, and a Kubernetes context.

Validation:

```bash
devspace run plan
devspace dev
devspace run-pipeline ai-handoff-check
```

### Phase 3: Standardize OTA Packaging

Goal: make OTA publication repeatable and verifiable.

Tasks:

- Add one command that builds firmware, copies `firmware.bin` to `cloud/firmware/`, and writes `manifest.json`.
- Verify manifest `size` and `md5` against the binary.
- Keep OTA installation as an explicit H5/operator action.
- Do not auto-trigger device install from build or deploy commands.

Validation:

```bash
pio run -d firmware
python tools/check_ai_handoff.py
```

### Phase 4: Convert tools_local To A Thin Control Center

Goal: keep the useful UI, remove duplicate deployment logic.

Allowed responsibilities:

- Show repo and environment status.
- Trigger safe Git commands.
- Trigger PlatformIO build/package commands.
- Trigger DevSpace commands.
- Open local/cloud H5 URLs.
- Trigger OTA only through explicit operator action.

Responsibilities to remove or downgrade:

- Hand-written cloud deploy logic.
- Direct PM2 lifecycle management as the main path.
- Hidden Git push/pull on startup.
- Any command that can discard local changes without clear confirmation.

### Phase 5: Optional AI Orchestration

Goal: integrate GPT-style planning with Codex execution without making DevSpace responsible for models.

DevSpace does not download or run GPT models by itself. If an AI orchestration service is needed, it should be a separate local or cloud service that can call OpenAI APIs or an agent SDK.

Possible future shape:

```text
local/agent service
  -> plans task
  -> calls Codex or prepares handoff
  -> Codex edits repo
  -> DevSpace/PlatformIO run validation
```

This is optional. The immediate priority is to make the engineering workflow reliable.

## Command Contract

These commands should be treated as stable building blocks:

```bash
devspace run plan
devspace run doctor
devspace dev
devspace run-pipeline firmware-check
devspace run-pipeline vision-check
devspace run-pipeline ai-handoff-check
```

Future commands to add:

```bash
devspace run doctor
devspace run cloud-check
devspace run cloud-logs
devspace run package-ota
devspace run release-check
```

`tools_local/` should call these commands rather than duplicate their logic.

## Codex Implementation Rules

When Codex continues from this document:

1. Read `AGENTS.md`, this file, and the relevant module docs before editing.
2. Keep each implementation step small and reviewable.
3. Prefer changing one workflow layer at a time.
4. Do not mix safety-critical firmware edits with DevSpace/tooling refactors.
5. Update `AI-HANDOFF-MEMORY.md` after every file change.
6. Run the closest available validation command.
7. If toolchain commands are missing, record the blocker and continue with static validation.

## GPT To Codex Handoff Template

```text
Goal:
Files:
Constraints:
Safety impact:
Validation:
Expected handoff update:
```

## Near-Term Backlog

1. Make `tools_local` Git pull safe and non-destructive.
2. Add `package-ota` command to DevSpace or a repo script.
3. Add `cloud-check` and `cloud-logs` commands.
4. Make the local control center call DevSpace for cloud actions.
5. Move private deployment values into ignored local config or environment variables.
6. Keep legacy SCP/PM2 path available until DevSpace deploy is verified.
