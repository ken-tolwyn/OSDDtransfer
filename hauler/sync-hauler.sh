#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# shellcheck source=../lib/common.sh
source "$SCRIPT_DIR/../lib/common.sh"

load_common_config "$SCRIPT_DIR"
load_config_file "$SCRIPT_DIR/settings.env"

require_command podman
require_command rsync

MANIFEST_MOUNT=/workspace/content-manifest.yaml

log "Syncing Hauler store with container ${HAULER_CONTAINER_IMAGE}"

ensure_dir "$HAULER_STORE_DIR"
podman run --rm \
  -v "${HAULER_STORE_DIR}:${HAULER_STORE_DIR}:Z" \
  -v "${HAULER_CONTENT_MANIFEST}:${MANIFEST_MOUNT}:Z,ro" \
  "$HAULER_CONTAINER_IMAGE" \
  store sync -s "$HAULER_STORE_DIR" -f "$MANIFEST_MOUNT" -p "${HAULER_PLATFORM}/${HAULER_ARCH}"

stage_transfer_dir "$HAULER_STORE_DIR" "$HAULER_TRANSFER_NAME"
