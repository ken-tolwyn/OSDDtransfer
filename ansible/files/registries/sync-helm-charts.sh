#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# shellcheck source=../common.sh
source "$SCRIPT_DIR/../common.sh"

load_common_config "$SCRIPT_DIR"
load_config_file "$SCRIPT_DIR/config.env"

require_command podman
require_command yq

CHART_WORK_DIR="${SCRIPT_DIR}/tmp/charts"

ensure_dir "$CHART_WORK_DIR"

run_helm() {
  podman run --rm \
    -v "${CHART_WORK_DIR}:/charts:Z" \
    -w /charts \
    "$HELM_CONTAINER_IMAGE" "$@"
}

pull_chart() {
  local name=$1
  local repo_url=$2
  local version=$3

  if [[ "$repo_url" == oci://* ]]; then
    if [[ -n "$version" ]]; then
      run_helm pull "${repo_url%/}/${name}" --version "$version" --destination /charts
    else
      run_helm pull "${repo_url%/}/${name}" --destination /charts
    fi
  else
    if [[ -n "$version" ]]; then
      run_helm pull "$name" --repo "$repo_url" --version "$version" --destination /charts
    else
      run_helm pull "$name" --repo "$repo_url" --destination /charts
    fi
  fi
}

push_chart() {
  local package_file=$1
  local target_path=$2
  local push_target="oci://${LOCAL_REGISTRY_HOST}:${LOCAL_REGISTRY_PORT}/${REGISTRY_CHART_NAMESPACE}/${target_path}"

  log "Pushing chart $(basename "$package_file") -> ${push_target}"
  run_helm push --plain-http "/charts/$(basename "$package_file")" "$push_target"
}

log "Syncing Helm charts from ${REGISTRY_CHART_LIST}"

while IFS= read -r chart_spec; do
  name=$(yq -r '.name' <<<"$chart_spec")
  repo_url=$(yq -r '.repoURL' <<<"$chart_spec")
  version=$(yq -r '.version // ""' <<<"$chart_spec")
  target_path=$(yq -r '.targetPath // "charts"' <<<"$chart_spec")

  log "Fetching chart ${name} from ${repo_url}${version:+ version ${version}} for ${REGISTRY_CHART_NAMESPACE}/${target_path}"
  rm -f "${CHART_WORK_DIR}/${name}-"*.tgz
  pull_chart "$name" "$repo_url" "$version"

  latest_package=$(ls -t "${CHART_WORK_DIR}/${name}-"*.tgz 2>/dev/null | head -n 1)
  [[ -n "${latest_package:-}" ]] || fail "No chart package found for ${name}"
  push_chart "$latest_package" "$target_path"
done < <(yq -o=json -I=0 '.charts[]' "$REGISTRY_CHART_LIST")
