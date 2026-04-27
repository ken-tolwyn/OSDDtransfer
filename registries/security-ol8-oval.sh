#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# shellcheck source=../lib/common.sh
source "$SCRIPT_DIR/../lib/common.sh"

load_common_config "$SCRIPT_DIR"
load_config_file "$SCRIPT_DIR/config.env"

require_command curl
require_command buildah

LOCAL_IMAGE="${LOCAL_REGISTRY_HOST}:${LOCAL_REGISTRY_PORT}/${LOCAL_REGISTRY_NAMESPACE}/${OL8_OVAL_IMAGE_REF}"

log "Refreshing Oracle Linux 8 OVAL database"
ensure_dir "$REGISTRY_SCAN_TMP_DIR"

(
  cd "$REGISTRY_SCAN_TMP_DIR"

  curl --fail -LO "$OL8_OVAL_URL"
  container=$(buildah from cgr.dev/chainguard/openscap:latest-dev)
  trap 'buildah rm "$container" >/dev/null 2>&1 || true' EXIT

  buildah config --workingdir /oval/ "$container"
  buildah copy "$container" "$OL8_OVAL_DB_FILE" /oval/
  buildah commit "$container" "$LOCAL_IMAGE"
  buildah push --tls-verify=false "$LOCAL_IMAGE"

  rm -f "$OL8_OVAL_DB_FILE"
)
