#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# shellcheck source=../lib/common.sh
source "$SCRIPT_DIR/../lib/common.sh"

load_common_config "$SCRIPT_DIR"
load_config_file "$SCRIPT_DIR/config.env"

require_command podman
require_command skopeo
require_command yq
require_command kubeadm
require_command rsync
require_command curl
require_command jq
require_command buildah
require_command zgrep
require_command gzip

REGISTRY_BASE="${LOCAL_REGISTRY_HOST}:${LOCAL_REGISTRY_PORT}/${LOCAL_REGISTRY_NAMESPACE}"
REGISTRY_STARTED_BY_SCRIPT=false
REGISTRY_ROOT="$TRUNK_ROOT/$PROJECT_LOCATION"

cleanup() {
  if [[ "$REGISTRY_STARTED_BY_SCRIPT" == "true" ]]; then
    podman rm -f "$REGISTRY_CONTAINER_NAME" >/dev/null 2>&1 || true
  fi
}

trap cleanup EXIT

ensure_registry_running() {
  if podman container exists "$REGISTRY_CONTAINER_NAME"; then
    if [[ "$(podman inspect -f '{{.State.Running}}' "$REGISTRY_CONTAINER_NAME")" == "true" ]]; then
      log "Using already running registry container ${REGISTRY_CONTAINER_NAME}"
      return 0
    fi

    log "Starting existing registry container ${REGISTRY_CONTAINER_NAME}"
    podman start "$REGISTRY_CONTAINER_NAME" >/dev/null
    return 0
  fi

  log "Starting Zot registry ${REGISTRY_CONTAINER_NAME}"
  podman run -d \
    --name "$REGISTRY_CONTAINER_NAME" \
    --restart=always \
    -p "${REGISTRY_LISTEN_PORT}:5000" \
    -v "${REGISTRY_STORAGE_DIR}/data:/var/lib/zot:Z" \
    -v "${REGISTRY_CONFIG_FILE}:/etc/zot/config.json:Z" \
    "$REGISTRY_IMAGE" \
    serve /etc/zot/config.json >/dev/null

  REGISTRY_STARTED_BY_SCRIPT=true
}

copy_image() {
  local source_ref=$1
  local destination_ref=$2

  log "Copying ${source_ref} -> ${destination_ref}"
  skopeo copy \
    --dest-tls-verify=false \
    -q \
    docker://${source_ref} \
    docker://${destination_ref} || echo "failed"
}

refresh_security_data() {
  local script

  for script in "$SCRIPT_DIR"/security-*.sh; do
    [[ -f "$script" ]] || continue
    log "Running $(basename "$script")"
    "$script"
  done
}

sync_secondary_registry_content() {
  local script

  for script in "$SCRIPT_DIR"/sync-helm-*.sh; do
    [[ -f "$script" ]] || continue
    log "Running $(basename "$script")"
    "$script"
  done
}

ensure_dir "$REGISTRY_ROOT/data"

ensure_registry_running
sleep 5
#while IFS= read -r registry; do
#  while IFS= read -r image; do
#    while IFS= read -r tag; do
#      copy_image \
#        "${registry}/${image}:${tag}" \
#        "${REGISTRY_BASE}/${image}:${tag}"
#    done < <(yq -r ".\"${registry}\".images.\"${image}\"[]" "$REGISTRY_IMAGE_LIST")
#  done < <(yq -r ".\"${registry}\".images | keys | .[]" "$REGISTRY_IMAGE_LIST")
#done < <(yq -r 'keys | .[]' "$REGISTRY_IMAGE_LIST")
#
#if [[ "${SYNC_KUBEADM_IMAGES:-true}" == "true" ]]; then
#  while IFS= read -r image; do
#    copy_image "$image" "${REGISTRY_BASE}/${image}"
#  done < <(kubeadm config images list --kubernetes-version "$KUBERNETES_VERSION")
#fi

#Disabled as security might need a different transfer requirement
#log "Refreshing scanner databases"
#refresh_security_data

if [[ "${SYNC_HELM_CHARTS:-true}" == "true" ]]; then
  log "Syncing Helm charts"
  sync_secondary_registry_content
fi

transfer_dir "$PROJECT_LOCATION" .
