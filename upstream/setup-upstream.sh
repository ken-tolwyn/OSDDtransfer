#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# shellcheck source=../lib/common.sh
source "$SCRIPT_DIR/../lib/common.sh"

load_common_config "$SCRIPT_DIR"
load_config_file "$SCRIPT_DIR/config.env"

ensure_managed_dir() {
  local dir_path=$1

  install -d -m "$UPSTREAM_DIR_MODE" "$dir_path"
  chmod "$UPSTREAM_DIR_MODE" "$dir_path"
}

ensure_managed_dir "$TRUNK_ROOT"
ensure_managed_dir "$TRANSFER_ROOT"

for item in "${UPSTREAM_TRANSFER_ITEMS[@]}"; do
  ensure_managed_dir "${TRUNK_ROOT}/${item}"
  ensure_managed_dir "${TRANSFER_ROOT}/${item}"
done

ensure_managed_dir "${TRUNK_ROOT}/registry/data"
ensure_managed_dir "$MAVEN_STORAGE_DIR"

log "Upstream directories are present under ${TRUNK_ROOT}"
