#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

HOST_TRUNK=${UPSTREAMD_TEST_TRUNK:-/home/ken/trunk}
CONFIG_PATH=${1:-container-config/upstreamd.toml}

rm -f "${HOST_TRUNK}/repository/grype-native-dry-run"

export UPSTREAMD_SYNC_DRY_RUN=true

bash "${REPO_ROOT}/upstream/test-upstreamd-in-container.sh" \
  "$CONFIG_PATH" \
  --run-repository-native

if [[ ! -f "${HOST_TRUNK}/repository/grype-native-dry-run" ]]; then
  printf 'grype native test failed: missing %s\n' \
    "${HOST_TRUNK}/repository/grype-native-dry-run" >&2
  exit 1
fi

printf 'grype native test passed: %s\n' \
  "${HOST_TRUNK}/repository/grype-native-dry-run"
