#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

HOST_TRUNK=${UPSTREAMD_TEST_TRUNK:-/home/ken/trunk}
CONFIG_PATH=${1:-upstream/testdata/upstreamd-container-test.toml}

rm -f "${HOST_TRUNK}/repository-sync-dry-run" "${HOST_TRUNK}/registry-sync-dry-run"

export UPSTREAMD_SYNC_DRY_RUN=true

bash "${REPO_ROOT}/upstream/test-upstreamd-in-container.sh" \
  "$CONFIG_PATH" \
  --run-sync \
  repositories

bash "${REPO_ROOT}/upstream/test-upstreamd-in-container.sh" \
  "$CONFIG_PATH" \
  --run-sync \
  registries

for marker in "${HOST_TRUNK}/repository-sync-dry-run" "${HOST_TRUNK}/registry-sync-dry-run"; do
  if [[ ! -f "$marker" ]]; then
    printf 'run-sync test failed: missing marker %s\n' "$marker" >&2
    exit 1
  fi
done

printf 'run-sync test passed: %s %s\n' \
  "${HOST_TRUNK}/repository-sync-dry-run" \
  "${HOST_TRUNK}/registry-sync-dry-run"
