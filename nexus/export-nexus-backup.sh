#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
CONFIG_FILE="${NEXUS_CONFIG_FILE:-$SCRIPT_DIR/config.env}"

# shellcheck source=../lib/common.sh
source "$SCRIPT_DIR/../lib/common.sh"

load_common_config "$SCRIPT_DIR"
load_config_file "$CONFIG_FILE"

create_metadata() {
  local metadata_file=$1

  cat >"$metadata_file" <<EOF
backup_name=${BACKUP_NAME}
created_at=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
source_dir=${NEXUS_SERVER_FILES_DIR}
archive_file=$(basename "$ARCHIVE_PATH")
checksum_file=$(basename "$CHECKSUM_PATH")
staged_to_transfer=${STAGE_TO_TRANSFER}
EOF
}

require_command tar
require_command sha256sum

: "${NEXUS_SERVER_FILES_DIR:?NEXUS_SERVER_FILES_DIR must be set in the config file}"

NEXUS_OUTPUT_DIR=${NEXUS_OUTPUT_DIR:-"$SCRIPT_DIR/output"}
STAGE_TO_TRANSFER=${STAGE_TO_TRANSFER:-true}
TRANSFER_ROOT=${TRANSFER_ROOT:-/trunk/transfer}
NEXUS_TRANSFER_NAME=${NEXUS_TRANSFER_NAME:-nexus}
BACKUP_NAME_PREFIX=${BACKUP_NAME_PREFIX:-nexus-backup}
TIMESTAMP_FORMAT=${TIMESTAMP_FORMAT:-%Y%m%d-%H%M%S}
ARCHIVE_SUFFIX=${ARCHIVE_SUFFIX:-tar.gz}

[[ -d "$NEXUS_SERVER_FILES_DIR" ]] || fail "NEXUS_SERVER_FILES_DIR does not exist or is not a directory: $NEXUS_SERVER_FILES_DIR"

mkdir -p "$NEXUS_OUTPUT_DIR"

TIMESTAMP=$(date +"$TIMESTAMP_FORMAT")
BACKUP_NAME="${BACKUP_NAME_PREFIX}-${TIMESTAMP}"
ARCHIVE_PATH="${NEXUS_OUTPUT_DIR%/}/${BACKUP_NAME}.${ARCHIVE_SUFFIX}"
CHECKSUM_PATH="${ARCHIVE_PATH}.sha256"
METADATA_PATH="${ARCHIVE_PATH}.metadata"

log "Creating Nexus backup archive from $NEXUS_SERVER_FILES_DIR"
tar -C "$NEXUS_SERVER_FILES_DIR" -czf "$ARCHIVE_PATH" .

log "Generating checksum"
(
  cd "$NEXUS_OUTPUT_DIR"
  sha256sum "$(basename "$ARCHIVE_PATH")" >"$(basename "$CHECKSUM_PATH")"
)

log "Writing metadata"
create_metadata "$METADATA_PATH"

if [[ "$STAGE_TO_TRANSFER" == "true" ]]; then
  stage_transfer_files "$NEXUS_TRANSFER_NAME" "$ARCHIVE_PATH" "$CHECKSUM_PATH" "$METADATA_PATH"
else
  log "Transfer staging disabled by config"
fi

log "Backup archive created: $ARCHIVE_PATH"
log "Checksum file created: $CHECKSUM_PATH"
log "Metadata file created: $METADATA_PATH"
