#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  ./reconcile-transfer.sh /path/to/target-dir [/path/to/.transfer-manifest.tsv]

Behavior:
  - verifies every listed file exists and matches the expected sha256
  - deletes files in the target directory that are not listed in the manifest
  - removes empty directories after deleting extra files
EOF
}

fail() {
  printf 'ERROR: %s\n' "$*" >&2
  exit 1
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" || $# -lt 1 ]]; then
  usage
  exit 0
fi

TARGET_DIR=$1
MANIFEST_FILE=${2:-"$TARGET_DIR/.transfer-manifest.tsv"}

[[ -d "$TARGET_DIR" ]] || fail "Target directory not found: $TARGET_DIR"
[[ -f "$MANIFEST_FILE" ]] || fail "Manifest file not found: $MANIFEST_FILE"

declare -A EXPECTED_FILES=()

while IFS=$'\t' read -r expected_sha relative_path; do
  [[ -n "${relative_path:-}" ]] || continue

  target_file="${TARGET_DIR%/}/${relative_path}"
  [[ -f "$target_file" ]] || fail "Manifest file missing on target: $relative_path"

  actual_sha=$(sha256sum "$target_file" | awk '{print $1}')
  [[ "$actual_sha" == "$expected_sha" ]] || fail "Checksum mismatch for $relative_path"

  EXPECTED_FILES["$relative_path"]=1
done < "$MANIFEST_FILE"

while IFS= read -r existing_file; do
  relative_path=${existing_file#./}

  if [[ "$relative_path" == ".transfer-manifest.tsv" ]]; then
    continue
  fi

  if [[ -z "${EXPECTED_FILES[$relative_path]:-}" ]]; then
    rm -f "${TARGET_DIR%/}/${relative_path}"
  fi
done < <(
  cd "$TARGET_DIR"
  find . -type f | LC_ALL=C sort
)

find "$TARGET_DIR" -depth -type d -empty -delete
