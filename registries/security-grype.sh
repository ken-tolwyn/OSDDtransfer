#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# shellcheck source=../lib/common.sh
source "$SCRIPT_DIR/../lib/common.sh"

load_common_config "$SCRIPT_DIR"
load_config_file "$SCRIPT_DIR/config.env"

require_command curl
require_command jq
require_command buildah

LOCAL_IMAGE="${LOCAL_REGISTRY_HOST}:${LOCAL_REGISTRY_PORT}/${LOCAL_REGISTRY_NAMESPACE}/${GRYPE_IMAGE_REF}"

log "Refreshing Grype database"
ensure_dir "$REGISTRY_SCAN_TMP_DIR"

(
  cd "$REGISTRY_SCAN_TMP_DIR"

  curl --fail -LO "${GRYPE_DB_SOURCE}/latest.json"
  DB_FILE=$(jq -r .path < latest.json)
  BUILT=$(jq -r .built < latest.json)
  curl --fail -LO "${GRYPE_DB_SOURCE}/${DB_FILE}"
  curl --fail -Lo junit.tmpl "$GRYPE_TEMPLATE_URL"
  log "Found Grype database ${DB_FILE} built ${BUILT}"

  container=$(buildah from cgr.dev/chainguard/nginx:latest)
  trap 'buildah rm "$container" >/dev/null 2>&1 || true' EXIT

  buildah copy "$container" latest.json /usr/share/nginx/html/v6/
  buildah copy "$container" "$DB_FILE" /usr/share/nginx/html/v6/
  buildah copy "$container" junit.tmpl "$REGISTRY_SCAN_LATEX_TEMPLATE" "$REGISTRY_SCAN_CSV_TEMPLATE" /usr/share/nginx/html/
  buildah commit "$container" "$LOCAL_IMAGE"
  buildah push --tls-verify=false "$LOCAL_IMAGE"

  rm -f "$DB_FILE" latest.json junit.tmpl
)
