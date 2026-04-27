#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# shellcheck source=../lib/common.sh
source "$SCRIPT_DIR/../lib/common.sh"

load_common_config "$SCRIPT_DIR"
load_config_file "$SCRIPT_DIR/config.env"

require_command curl
require_command jq
require_command yq
require_command kubeadm

usage() {
  cat <<'EOF'
Usage:
  ./reconcile-zot-images.sh [--apply]

Behavior:
  - fetches the current repo/tag list from Zot under the configured namespace
  - compares it to the desired image set from outside.yaml
  - optionally includes kubeadm images when SYNC_KUBEADM_IMAGES=true
  - excludes configured chart and scanner repos from deletion
  - defaults to dry-run and prints the planned deletions
EOF
}

DRY_RUN=true
if [[ ${1:-} == "--apply" ]]; then
  DRY_RUN=false
elif [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
  usage
  exit 0
elif [[ $# -gt 0 ]]; then
  fail "Unknown argument: $1"
fi

REGISTRY_API="http://${LOCAL_REGISTRY_HOST}:${LOCAL_REGISTRY_PORT}/v2"
declare -A DESIRED_TAGS=()
declare -A LIVE_TAGS=()

api_get() {
  local url=$1
  curl --fail --silent --show-error "$url"
}

is_excluded_repo() {
  local repo=$1
  local prefix
  local exact

  for prefix in "${REGISTRY_RECONCILE_EXCLUDE_PREFIXES[@]:-}"; do
    if [[ "$repo" == "$prefix"* ]]; then
      return 0
    fi
  done

  for exact in "${REGISTRY_RECONCILE_EXCLUDE_REPOS[@]:-}"; do
    if [[ "$repo" == "$exact" ]]; then
      return 0
    fi
  done

  return 1
}

add_desired_tag() {
  local repo=$1
  local tag=$2
  DESIRED_TAGS["${repo}:${tag}"]=1
}

load_desired_images() {
  while IFS= read -r registry; do
    while IFS= read -r image; do
      while IFS= read -r tag; do
        add_desired_tag "$image" "$tag"
      done < <(yq -r ".\"${registry}\".images.\"${image}\"[]" "$REGISTRY_IMAGE_LIST")
    done < <(yq -r ".\"${registry}\".images | keys | .[]" "$REGISTRY_IMAGE_LIST")
  done < <(yq -r 'keys | .[]' "$REGISTRY_IMAGE_LIST")

  if [[ "${SYNC_KUBEADM_IMAGES:-true}" == "true" ]]; then
    while IFS= read -r image_ref; do
      repo=${image_ref%:*}
      tag=${image_ref##*:}
      repo=${repo#*/}
      add_desired_tag "$repo" "$tag"
    done < <(kubeadm config images list --kubernetes-version "$KUBERNETES_VERSION")
  fi
}

load_live_images() {
  local catalog_json repo tags_json tag

  catalog_json=$(api_get "${REGISTRY_API}/_catalog?n=10000")
  while IFS= read -r repo; do
    [[ "$repo" == "${LOCAL_REGISTRY_NAMESPACE}/"* ]] || continue
    repo=${repo#${LOCAL_REGISTRY_NAMESPACE}/}

    if is_excluded_repo "$repo"; then
      continue
    fi

    tags_json=$(api_get "${REGISTRY_API}/${LOCAL_REGISTRY_NAMESPACE}/${repo}/tags/list" || true)
    [[ -n "$tags_json" ]] || continue

    while IFS= read -r tag; do
      [[ -n "$tag" && "$tag" != "null" ]] || continue
      LIVE_TAGS["${repo}:${tag}"]=1
    done < <(jq -r '.tags[]?' <<<"$tags_json")
  done < <(jq -r '.repositories[]?' <<<"$catalog_json")
}

delete_tag() {
  local repo=$1
  local tag=$2
  local manifest_headers digest

  manifest_headers=$(curl --silent --show-error --head \
    -H 'Accept: application/vnd.oci.image.manifest.v1+json, application/vnd.docker.distribution.manifest.v2+json, application/vnd.oci.image.index.v1+json, application/vnd.docker.distribution.manifest.list.v2+json' \
    "${REGISTRY_API}/${LOCAL_REGISTRY_NAMESPACE}/${repo}/manifests/${tag}")

  digest=$(awk 'BEGIN{IGNORECASE=1} /^Docker-Content-Digest:/ {gsub("\r","",$2); print $2}' <<<"$manifest_headers")
  [[ -n "$digest" ]] || fail "Unable to resolve digest for ${repo}:${tag}"

  if [[ "$DRY_RUN" == "true" ]]; then
    log "DRY-RUN delete ${LOCAL_REGISTRY_NAMESPACE}/${repo}:${tag} (${digest})"
  else
    log "Deleting ${LOCAL_REGISTRY_NAMESPACE}/${repo}:${tag} (${digest})"
    curl --fail --silent --show-error -X DELETE "${REGISTRY_API}/${LOCAL_REGISTRY_NAMESPACE}/${repo}/manifests/${digest}" >/dev/null
  fi
}

load_desired_images
load_live_images

deletions=0
for live_ref in "${!LIVE_TAGS[@]}"; do
  if [[ -z "${DESIRED_TAGS[$live_ref]:-}" ]]; then
    repo=${live_ref%:*}
    tag=${live_ref##*:}
    delete_tag "$repo" "$tag"
    deletions=$((deletions + 1))
  fi
done

if [[ "$deletions" -eq 0 ]]; then
  log "No extra image tags found in Zot"
elif [[ "$DRY_RUN" == "true" ]]; then
  log "Dry-run found ${deletions} image tag deletions"
else
  log "Applied ${deletions} image tag deletions"
fi
