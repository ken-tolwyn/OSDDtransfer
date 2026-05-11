#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

HOST_TRUNK=${UPSTREAMD_TEST_TRUNK:-/home/ken/trunk}
CONFIG_PATH=${1:-upstream/testdata/upstreamd-scheduler-container-test.toml}
RUN_SECONDS=${UPSTREAMD_SCHEDULER_SECONDS:-3}

rm -f "${HOST_TRUNK}/repository-sync-ran" "${HOST_TRUNK}/registry-sync-ran"

bash "${REPO_ROOT}/upstream/test-upstreamd-in-container.sh" \
  "$CONFIG_PATH" \
  --run-scheduler \
  "$RUN_SECONDS"

for marker in "${HOST_TRUNK}/repository-sync-ran" "${HOST_TRUNK}/registry-sync-ran"; do
  if [[ ! -f "$marker" ]]; then
    printf 'scheduler test failed: missing marker %s\n' "$marker" >&2
    exit 1
  fi
done

printf 'scheduler test passed: %s %s\n' \
  "${HOST_TRUNK}/repository-sync-ran" \
  "${HOST_TRUNK}/registry-sync-ran"
