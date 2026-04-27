#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# shellcheck source=../lib/common.sh
source "$SCRIPT_DIR/../lib/common.sh"

load_common_config "$SCRIPT_DIR"
load_config_file "$SCRIPT_DIR/config.env"

LOCAL_REGISTRY="${LOCAL_REGISTRY_HOST}:${LOCAL_REGISTRY_PORT}/${LOCAL_REGISTRY_NAMESPACE}"

log "Refreshing Trivy databases"

for image in "${TRIVY_IMAGES[@]}"; do
  run_oras cp --to-plain-http "${TRIVY_DB_SOURCE}/${image}" "${LOCAL_REGISTRY}/${image}"
done
