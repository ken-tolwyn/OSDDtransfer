#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# shellcheck source=../lib/common.sh
source "$SCRIPT_DIR/../lib/common.sh"

load_common_config "$SCRIPT_DIR"
load_config_file "$SCRIPT_DIR/config.env"

require_command skopeo
require_command yq
require_command kubeadm
require_command podman

REPORT_DIR="${SCRIPT_DIR}/tmp/prefetch"
REPORT_FILE="${REPORT_DIR}/registry-source-check.tsv"
FAILED_REPORT_FILE="${REPORT_DIR}/registry-source-check-failed.tsv"

ensure_dir "$REPORT_DIR"

run_helm() {
  podman run --rm \
    "$HELM_CONTAINER_IMAGE" "$@"
}

append_report() {
  local kind=$1
  local source=$2
  local version=$3
  local status=$4
  local note=$5

  printf '%s\t%s\t%s\t%s\t%s\n' "$kind" "$source" "$version" "$status" "$note" >> "$REPORT_FILE"
}

check_image_ref() {
  local source=$1
  local version=$2
  local ref="${source}:${version}"

  if skopeo inspect "docker://${ref}" >/dev/null 2>&1; then
    append_report image "$source" "$version" ok ""
  else
    append_report image "$source" "$version" failed "skopeo inspect failed"
  fi
}

check_chart_ref() {
  local name=$1
  local repo_url=$2
  local version=$3
  local target_path=$4

  if [[ "$repo_url" == oci://* ]]; then
    if [[ -n "$version" ]]; then
      if run_helm show chart "${repo_url%/}/${name}" --version "$version" >/dev/null 2>&1; then
        append_report chart "${repo_url%/}/${name}" "$version" ok "$target_path"
      else
        append_report chart "${repo_url%/}/${name}" "$version" failed "$target_path"
      fi
    else
      if run_helm show chart "${repo_url%/}/${name}" >/dev/null 2>&1; then
        append_report chart "${repo_url%/}/${name}" latest ok "$target_path"
      else
        append_report chart "${repo_url%/}/${name}" latest failed "$target_path"
      fi
    fi
  else
    if [[ -n "$version" ]]; then
      if run_helm show chart "$name" --repo "$repo_url" --version "$version" >/dev/null 2>&1; then
        append_report chart "$repo_url/$name" "$version" ok "$target_path"
      else
        append_report chart "$repo_url/$name" "$version" failed "$target_path"
      fi
    else
      if run_helm show chart "$name" --repo "$repo_url" >/dev/null 2>&1; then
        append_report chart "$repo_url/$name" latest ok "$target_path"
      else
        append_report chart "$repo_url/$name" latest failed "$target_path"
      fi
    fi
  fi
}

check_configured_images() {
  while IFS= read -r registry; do
    while IFS= read -r image; do
      while IFS= read -r tag; do
        check_image_ref "${registry}/${image}" "$tag"
      done < <(yq -r ".\"${registry}\".images.\"${image}\"[]" "$REGISTRY_IMAGE_LIST")
    done < <(yq -r ".\"${registry}\".images | keys | .[]" "$REGISTRY_IMAGE_LIST")
  done < <(yq -r 'keys | .[]' "$REGISTRY_IMAGE_LIST")
}

check_kubeadm_images() {
  if [[ "${SYNC_KUBEADM_IMAGES:-true}" != "true" ]]; then
    return 0
  fi

  while IFS= read -r image_ref; do
    source=${image_ref%:*}
    version=${image_ref##*:}
    check_image_ref "$source" "$version"
  done < <(kubeadm config images list --kubernetes-version "$KUBERNETES_VERSION")
}

check_charts() {
  if [[ "${SYNC_HELM_CHARTS:-true}" != "true" ]]; then
    return 0
  fi

  while IFS= read -r chart_spec; do
    name=$(yq -r '.name' <<<"$chart_spec")
    repo_url=$(yq -r '.repoURL' <<<"$chart_spec")
    version=$(yq -r '.version // ""' <<<"$chart_spec")
    target_path=$(yq -r '.targetPath // "charts"' <<<"$chart_spec")
    check_chart_ref "$name" "$repo_url" "$version" "$target_path"
  done < <(yq -o=json '.charts[]' "$REGISTRY_CHART_LIST")
}

printf 'kind\tsource\tversion\tstatus\tnote\n' > "$REPORT_FILE"

log "Checking configured registry image sources"
check_configured_images

log "Checking kubeadm image sources"
check_kubeadm_images

log "Checking chart sources"
check_charts

failures=$(awk -F '\t' 'NR > 1 && $4 != "ok" {count++} END {print count+0}' "$REPORT_FILE")
total=$(awk 'END {print NR-1}' "$REPORT_FILE")

awk -F '\t' 'NR == 1 || $4 != "ok"' "$REPORT_FILE" > "$FAILED_REPORT_FILE"

log "Wrote source check report to ${REPORT_FILE}"
log "Wrote failed-only source check report to ${FAILED_REPORT_FILE}"
log "Checked ${total} source entries"

if [[ "$failures" -gt 0 ]]; then
  log "Found ${failures} failed source checks"
  exit 1
fi

log "All configured source checks passed"
