#!/usr/bin/env bash
# Reliable runner for WebGL text rendering e2e test.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${PLAYWRIGHT_PORT:-9876}"
BASE_URL="http://localhost:${PORT}"

cd "$ROOT"

echo "==> Building web bundle..."
npm run build:web-only

echo "==> Starting dist server on ${PORT}..."
npx --yes http-server dist -p "${PORT}" -c-1 --cors &
SERVER_PID=$!
trap 'kill "${SERVER_PID}" 2>/dev/null || true' EXIT
sleep 1

for _ in $(seq 1 30); do
  if curl -sf "${BASE_URL}/" >/dev/null 2>&1; then
    break
  fi
  sleep 0.2
done

if ! curl -sf "${BASE_URL}/" >/dev/null 2>&1; then
  echo "ERROR: dist server did not start on ${BASE_URL}" >&2
  exit 1
fi

echo "==> Running Playwright (WebGL text regression)..."
PLAYWRIGHT_SKIP_WEBSERVER=1 PLAYWRIGHT_BASE_URL="${BASE_URL}" \
  npx playwright test tests/e2e/text-rendering.spec.ts --project=chromium --grep webgl "$@"