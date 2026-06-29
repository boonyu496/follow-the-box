---
name: followbox-cloud-h5-deploy
description: Use for FollowBox cloud H5, cloud/server.js, cloud/public assets, DevSpace, Kubernetes, PM2, SCP/SSH deployment, cloud firmware publication, or any task that might deploy web files. Enforces project isolation so other cloud projects are not overwritten or polluted.
---

# FollowBox Cloud H5 Deploy

## Boundaries

- Cloud service source: `cloud/`.
- Cloud H5 source: `cloud/public/`.
- Cloud OTA artifact directory: `cloud/firmware/`.
- Kubernetes namespace: `followbox-dev` unless the user explicitly names another namespace.
- DevSpace deployment name: `followbox-cloud`.
- Embedded device H5 is separate: `firmware/web/` and `firmware/data/`.

## Deployment Rules

- Never copy, rsync, scp, or delete a parent directory such as `/www/wwwroot`, `/www/server`, repo root, user home, or a directory that contains multiple projects.
- Remote writes must target only the FollowBox cloud root, normally a path ending in `followbox-cloud`.
- Restart only the FollowBox cloud service/process, never all PM2 processes or unrelated services.
- Exclude secrets and local-only files: `.env`, `.env.local`, PEM/key files, `node_modules/`, logs, caches, and unrelated project folders.
- Validate `cloud/public/deploy-version.txt` or `/api/health` after deploy.
- Do not expose device/operator tokens in H5 source, logs, screenshots, or handoff records.
- Cloud H5 may request low-speed commands and OTA install consent only through existing APIs; it must not set PWM, clear safety locks, or bypass firmware setup gates.

## Preferred Commands

Use DevSpace for cloud development when available:

```bash
devspace run cloud-check
devspace dev
devspace run-pipeline ai-handoff-check
```

For static validation:

```bash
node --check cloud/server.js cloud/public/app.js
python tools/check_ai_handoff.py
python tools/check_verified_locks.py
```

## Handoff

Any cloud H5 deploy or deploy-rule change must record:

- Source path.
- Target path or namespace.
- Service/process restarted.
- Health endpoint result.
- Why no other project path was touched.
