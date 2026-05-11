#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

HOST_TRUNK=${UPSTREAMD_TEST_TRUNK:-/home/ken/trunk}
CONFIG_PATH=${1:-container-config/upstreamd.toml}

rm -f "${HOST_TRUNK}/repository/reposync-native-dry-run" \
      "${HOST_TRUNK}/repository/reposync-native-count"

export UPSTREAMD_SYNC_DRY_RUN=true

bash "${REPO_ROOT}/upstream/test-upstreamd-in-container.sh" \
  "$CONFIG_PATH" \
  --run-repository-native

for required in \
  "${HOST_TRUNK}/repository/reposync-native-dry-run" \
  "${HOST_TRUNK}/repository/reposync-native-count"; do
  if [[ ! -f "$required" ]]; then
    printf 'reposync native test failed: missing %s\n' "$required" >&2
    exit 1
  fi
done

grep -q '^1$' "${HOST_TRUNK}/repository/reposync-native-count"

printf 'reposync native test passed: %s\n' \
  "${HOST_TRUNK}/repository/reposync-native-count"
