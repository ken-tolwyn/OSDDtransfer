#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

HOST_TRUNK=${UPSTREAMD_TEST_TRUNK:-/home/ken/trunk}
WATCH_SECONDS=${UPSTREAMD_WATCH_SECONDS:-8}
CONFIG_PATH=${1:-upstream/testdata/upstreamd-container-test.toml}

TOKEN=$(date +%s)
RELATIVE_PATH="watch-tests/${TOKEN}/event.txt"
SOURCE_FILE="${HOST_TRUNK}/repository/${RELATIVE_PATH}"
TRANSFER_FILE="${HOST_TRUNK}/transfer/repository/${RELATIVE_PATH}"

mkdir -p "$(dirname "$SOURCE_FILE")"

bash "${REPO_ROOT}/upstream/test-upstreamd-in-container.sh" \
  "$CONFIG_PATH" \
  --watch \
  "$WATCH_SECONDS" &
WATCH_PID=$!

sleep 2
printf 'watch test %s\n' "$TOKEN" >"$SOURCE_FILE"

wait "$WATCH_PID"

if [[ ! -f "$TRANSFER_FILE" ]]; then
  printf 'watch test failed: missing transfer file %s\n' "$TRANSFER_FILE" >&2
  exit 1
fi

SOURCE_INODE=$(stat -c '%i' "$SOURCE_FILE")
TRANSFER_INODE=$(stat -c '%i' "$TRANSFER_FILE")

if [[ "$SOURCE_INODE" != "$TRANSFER_INODE" ]]; then
  printf 'watch test failed: %s and %s are not hard linked\n' \
    "$SOURCE_FILE" "$TRANSFER_FILE" >&2
  exit 1
fi

printf 'watch test passed: %s -> %s\n' "$SOURCE_FILE" "$TRANSFER_FILE"
