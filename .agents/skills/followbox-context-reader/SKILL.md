---
name: followbox-context-reader
description: Use before FollowBox work when Codex or another AI needs to read the project quickly without wasting tokens. Selects the smallest safe context set for coding, review, debugging, H5/cloud deployment, documentation, wiring, or safety tasks.
---

# FollowBox Context Reader

## Minimal Context Sets

Always start with:

- `AGENTS.md`
- `AI-HANDOFF-MEMORY.md`
- `skills/README.md`
- `VERIFIED-LOCKS.md`

For read-only answers, add only the directly relevant current spec or source file.

For firmware, GPIO, safety, power, OTA, or movement work, also read:

- `FIRMWARE-SPEC.md`
- `CURRENT-WIRING-AI.md`
- `PIN-MAP-V1.md`
- `profiles/example_bldc_analog_36v.yaml`
- Relevant `protocols/*.md`, `POLARITY-DEFINITIONS.md`, `PWM-OUTPUT-CALIBRATION.md`, or `ESTOP-FEEDBACK-CIRCUIT.md`

For cloud H5 work, also read:

- `DEVSPACE-AI-WORKFLOW.md`
- `devspace.yaml`
- `cloud/server.js`
- `cloud/public/app.js`
- `cloud/public/index.html`

For embedded H5 work, also read:

- `firmware/web/index.html`
- `firmware/web/app.js`
- `firmware/web/style.css`
- `protocols/H5-API.md`

## Output

Before editing, state:

- Task type.
- Risk level: `low`, `medium`, `high`, or `safety-critical`.
- Files read.
- Files likely to change.
- Validation commands.

Keep this summary short. The purpose is to avoid full-repo rereads and stale assumptions.
