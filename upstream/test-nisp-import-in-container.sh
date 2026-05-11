#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

HOST_TRUNK=${UPSTREAMD_TEST_TRUNK:-/home/ken/trunk}
CONFIG_PATH=${1:-upstream/testdata/upstreamd-container-test.toml}
ISO_DIR="${REPO_ROOT}/upstream/testdata/config/repositories/iso"
ISO_PATH="${ISO_DIR}/NU-NISP-test.iso"
STAGING_DIR="${ISO_DIR}/nisp-fixture"
VERSION="NU-NISP542_OL"

rm -rf "${HOST_TRUNK}/repository/${VERSION}" \
       "${HOST_TRUNK}/transfer/repository/${VERSION}" \
       "${STAGING_DIR}"
rm -f "${HOST_TRUNK}/repository/${VERSION}.repo" \
      "${HOST_TRUNK}/transfer/repository/${VERSION}.repo" \
      "${ISO_PATH}"

mkdir -p "${STAGING_DIR}/baseos/repodata"
mkdir -p "${STAGING_DIR}/appstream/repodata"
mkdir -p "${STAGING_DIR}/nested/addons/repodata"
printf 'MEDIA: %s\n' "${VERSION}" > "${STAGING_DIR}/nisp.version"
printf '<repomd/>\n' > "${STAGING_DIR}/baseos/repodata/repomd.xml"
printf '<repomd/>\n' > "${STAGING_DIR}/appstream/repodata/repomd.xml"
printf '<repomd/>\n' > "${STAGING_DIR}/nested/addons/repodata/repomd.xml"
printf 'NISP-KEY\n' > "${STAGING_DIR}/TEST-GPG-KEY"
tar -cf "${ISO_PATH}" -C "${STAGING_DIR}" .
rm -rf "${STAGING_DIR}"

bash "${REPO_ROOT}/upstream/test-upstreamd-in-container.sh" \
  "$CONFIG_PATH" \
  --run-repository-native

for required in \
  "${HOST_TRUNK}/repository/${VERSION}/baseos/repodata/repomd.xml" \
  "${HOST_TRUNK}/repository/${VERSION}/appstream/repodata/repomd.xml" \
  "${HOST_TRUNK}/repository/${VERSION}/nested/addons/repodata/repomd.xml" \
  "${HOST_TRUNK}/repository/${VERSION}.repo" \
  "${HOST_TRUNK}/repository/keys/TEST-GPG-KEY" \
  "${HOST_TRUNK}/transfer/repository/${VERSION}.repo"; do
  if [[ ! -f "$required" ]]; then
    printf 'NISP native test failed: missing %s\n' "$required" >&2
    exit 1
  fi
done

if [[ ! -e "${ISO_PATH}" ]]; then
  printf 'NISP native test failed: ISO should have been preserved %s\n' "${ISO_PATH}" >&2
  exit 1
fi

grep -q "baseurl=https://repo.k.mis.local/${VERSION}/baseos" \
  "${HOST_TRUNK}/repository/${VERSION}.repo"
grep -q "baseurl=https://repo.k.mis.local/${VERSION}/appstream" \
  "${HOST_TRUNK}/repository/${VERSION}.repo"
grep -q "baseurl=https://repo.k.mis.local/${VERSION}/nested/addons" \
  "${HOST_TRUNK}/repository/${VERSION}.repo"
grep -q '^\[nested-addons\]$' "${HOST_TRUNK}/repository/${VERSION}.repo"
grep -q 'gpgkey=https://repo.k.mis.local/keys/RPM-GPG-KEY-test https://repo.k.mis.local/keys/TEST-GPG-KEY' \
  "${HOST_TRUNK}/repository/${VERSION}.repo"

printf 'NISP native test passed: %s\n' "${HOST_TRUNK}/repository/${VERSION}.repo"
