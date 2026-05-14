#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

HOST_TRUNK=${UPSTREAMD_TEST_TRUNK:-/home/ken/trunk}
CONFIG_PATH=${1:-upstreamd.toml}

rm -f "${HOST_TRUNK}/registry-images-native-dry-run" \
      "${HOST_TRUNK}/registry-images-native-count" \
      "${HOST_TRUNK}/registry-trivy-native-dry-run" \
      "${HOST_TRUNK}/registry-trivy-native-count" \
      "${HOST_TRUNK}/registry-charts-native-dry-run" \
      "${HOST_TRUNK}/registry-charts-native-count" \
      "${HOST_TRUNK}/registry-oval-native-dry-run" \
      "${HOST_TRUNK}/registry-oval-native-count"

export UPSTREAMD_SYNC_DRY_RUN=true

bash "${REPO_ROOT}/upstream/test-upstreamd-in-container.sh" \
  "$CONFIG_PATH" \
  --run-registry-native

for required in \
  "${HOST_TRUNK}/registry-images-native-dry-run" \
  "${HOST_TRUNK}/registry-trivy-native-dry-run" \
  "${HOST_TRUNK}/registry-charts-native-dry-run" \
  "${HOST_TRUNK}/registry-oval-native-dry-run" \
  "${HOST_TRUNK}/registry-images-native-count" \
  "${HOST_TRUNK}/registry-trivy-native-count" \
  "${HOST_TRUNK}/registry-charts-native-count" \
  "${HOST_TRUNK}/registry-oval-native-count"; do
  if [[ ! -f "$required" ]]; then
    printf 'registry native test failed: missing %s\n' "$required" >&2
    exit 1
  fi
done

grep -q '^2$' "${HOST_TRUNK}/registry-images-native-count"
grep -q '^2$' "${HOST_TRUNK}/registry-trivy-native-count"
grep -q '^2$' "${HOST_TRUNK}/registry-charts-native-count"
grep -q '^1$' "${HOST_TRUNK}/registry-oval-native-count"

printf 'registry native test passed: %s %s %s %s\n' \
  "${HOST_TRUNK}/registry-images-native-count" \
  "${HOST_TRUNK}/registry-trivy-native-count" \
  "${HOST_TRUNK}/registry-charts-native-count" \
  "${HOST_TRUNK}/registry-oval-native-count"
