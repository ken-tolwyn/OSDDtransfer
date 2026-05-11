#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

HOST_TRUNK=${UPSTREAMD_TEST_TRUNK:-/home/ken/trunk}
WATCH_SECONDS=${UPSTREAMD_WATCH_SECONDS:-8}
CONFIG_PATH=${1:-upstream/testdata/upstreamd-container-test.toml}

TOKEN=$(date +%s)
RELATIVE_PATH="watch-debug/${TOKEN}/event.txt"
SOURCE_FILE="${HOST_TRUNK}/repository/${RELATIVE_PATH}"
LOG_FILE="${HOST_TRUNK}/watch-debug-${TOKEN}.log"

mkdir -p "$(dirname "$SOURCE_FILE")"
rm -f "$LOG_FILE"

export UPSTREAMD_WATCH_DEBUG=true

bash "${REPO_ROOT}/upstream/test-upstreamd-in-container.sh" \
  "$CONFIG_PATH" \
  --watch \
  "$WATCH_SECONDS" >"$LOG_FILE" 2>&1 &
WATCH_PID=$!

sleep 2
printf 'watch debug %s\n' "$TOKEN" >"$SOURCE_FILE"

wait "$WATCH_PID"

printf 'debug log: %s\n' "$LOG_FILE"
cat "$LOG_FILE"
