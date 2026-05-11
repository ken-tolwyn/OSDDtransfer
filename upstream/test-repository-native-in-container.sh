#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

HOST_TRUNK=${UPSTREAMD_TEST_TRUNK:-/home/ken/trunk}
CONFIG_PATH=${1:-upstream/testdata/upstreamd-container-test.toml}

rm -rf "${HOST_TRUNK}/repository/OL8" \
       "${HOST_TRUNK}/repository/keys" \
       "${HOST_TRUNK}/transfer/repository/OL8" \
       "${HOST_TRUNK}/transfer/repository/keys"
rm -f "${HOST_TRUNK}/repository/OL8.repo" \
      "${HOST_TRUNK}/repository/list" \
      "${HOST_TRUNK}/transfer/repository/OL8.repo" \
      "${HOST_TRUNK}/transfer/repository/list"

mkdir -p "${HOST_TRUNK}/repository/OL8/baseos/repodata"
printf '<repomd/>\n' > "${HOST_TRUNK}/repository/OL8/baseos/repodata/repomd.xml"

bash "${REPO_ROOT}/upstream/test-upstreamd-in-container.sh" \
  "$CONFIG_PATH" \
  --run-repository-native

for required in \
  "${HOST_TRUNK}/repository/OL8.repo" \
  "${HOST_TRUNK}/repository/list" \
  "${HOST_TRUNK}/repository/keys/list" \
  "${HOST_TRUNK}/repository/keys/RPM-GPG-KEY-test" \
  "${HOST_TRUNK}/transfer/repository/OL8.repo" \
  "${HOST_TRUNK}/transfer/repository/list"; do
  if [[ ! -f "$required" ]]; then
    printf 'repository native test failed: missing %s\n' "$required" >&2
    exit 1
  fi
done

grep -q '^baseos$' "${HOST_TRUNK}/repository/OL8.repo" && {
  printf 'repository native test failed: malformed repo file\n' >&2
  exit 1
} || true

grep -q 'baseurl=https://repo.k.mis.local/OL8/baseos' "${HOST_TRUNK}/repository/OL8.repo"
grep -q 'gpgkey=https://repo.k.mis.local/keys/RPM-GPG-KEY-test' "${HOST_TRUNK}/repository/OL8.repo"
grep -q '^OL8.repo$' "${HOST_TRUNK}/repository/list"
grep -q '^RPM-GPG-KEY-test$' "${HOST_TRUNK}/repository/keys/list"

printf 'repository native test passed: %s\n' "${HOST_TRUNK}/repository/OL8.repo"
