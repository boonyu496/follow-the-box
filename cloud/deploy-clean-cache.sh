#!/usr/bin/env bash
set -euo pipefail

APP_DIR="${1:-/www/wwwroot/followbox-cloud}"
APP_NAME="${FOLLOWBOX_PM2_NAME:-followbox-cloud}"

cd "$APP_DIR"

# Stamp every deploy so index.html serves app/style URLs with a fresh version
# query. This avoids stale browser/CDN assets when the remote public/ folder is
# replaced but an old app.js is still cached by a client or proxy.
mkdir -p public
{
  echo "FollowBox cloud deploy package"
  echo "built_at=$(date -Iseconds)"
  echo "cache_policy=no-store+versioned-assets"
  echo "path=https://www.boonai.cn/fb/"
} > public/deploy-version.txt

# Static files are served with no-store headers. Remove common local caches so
# the newly uploaded files are read from disk immediately.
rm -rf .cache tmp cache public/.cache public/.vite 2>/dev/null || true

if command -v pm2 >/dev/null 2>&1; then
  pm2 flush "$APP_NAME" >/dev/null 2>&1 || true
  pm2 restart "$APP_NAME" --update-env || pm2 start server.js --name "$APP_NAME" --update-env
  pm2 save || true
else
  echo "pm2 not found; start manually with: node server.js" >&2
fi

if command -v nginx >/dev/null 2>&1; then
  nginx -t && (nginx -s reload || systemctl reload nginx || true)
fi

echo "FollowBox cloud deploy cache cleanup complete."
