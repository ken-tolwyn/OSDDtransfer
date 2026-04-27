#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
CONFIG_FILE="${NEXUS_CONFIG_FILE:-$SCRIPT_DIR/config.env}"

# shellcheck source=../lib/common.sh
source "$SCRIPT_DIR/../lib/common.sh"

load_common_config "$SCRIPT_DIR"

usage() {
  cat <<'EOF'
Usage:
  ./restore-nexus-backup.sh /path/to/nexus-backup-YYYYMMDD-HHMMSS.tar.gz [target-dir]

Behavior:
  - verifies the archive checksum when a matching .sha256 file exists
  - restores into the provided target directory
  - if no target directory is provided, uses NEXUS_SERVER_FILES_DIR from nexus/config.env
EOF
}

load_config_if_present() {
  if [[ -f "$CONFIG_FILE" ]]; then
    load_config_file "$CONFIG_FILE"
  fi
}

require_command tar
require_command sha256sum

if [[ ${1:-} == "-h" || ${1:-} == "--help" || $# -lt 1 ]]; then
  usage
  exit 0
fi

ARCHIVE_PATH=$1
TARGET_DIR=${2:-}

[[ -f "$ARCHIVE_PATH" ]] || fail "Archive not found: $ARCHIVE_PATH"

load_config_if_present

if [[ -z "$TARGET_DIR" ]]; then
  TARGET_DIR=${NEXUS_SERVER_FILES_DIR:-}
fi

[[ -n "$TARGET_DIR" ]] || fail "Target directory not provided and NEXUS_SERVER_FILES_DIR is not set"

CHECKSUM_PATH="${ARCHIVE_PATH}.sha256"

if [[ -f "$CHECKSUM_PATH" ]]; then
  log "Verifying checksum"
  (
    cd "$(dirname "$ARCHIVE_PATH")"
    sha256sum -c "$(basename "$CHECKSUM_PATH")"
  )
else
  log "Checksum file not found next to archive, skipping verification"
fi

mkdir -p "$TARGET_DIR"

log "Restoring archive into $TARGET_DIR"
tar -C "$TARGET_DIR" -xzf "$ARCHIVE_PATH"

log "Restore completed"
