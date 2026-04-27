#!/usr/bin/env bash

COMMON_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

log() {
  printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"
}

fail() {
  log "ERROR: $*"
  exit 1
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || fail "Required command not found: $1"
}

load_config_file() {
  local config_file=$1

  [[ -f "$config_file" ]] || fail "Config file not found: $config_file"

  # shellcheck disable=SC1090
  source "$config_file"
}

find_upwards() {
  local start_dir=$1
  local target_name=$2
  local current_dir

  current_dir=$(cd "$start_dir" && pwd)

  while [[ "$current_dir" != "/" ]]; do
    if [[ -f "$current_dir/$target_name" ]]; then
      printf '%s\n' "$current_dir/$target_name"
      return 0
    fi
    current_dir=$(dirname "$current_dir")
  done

  return 1
}

load_common_config() {
  local start_dir=${1:-$(pwd)}
  local config_file

  config_file=$(find_upwards "$start_dir" config.env) || fail "Unable to locate config.env from $start_dir"
  load_config_file "$config_file"
}

ensure_dir() {
  mkdir -p "$1"
}

write_manifest_from_dir() {
  local source_dir=$1
  local manifest_file=$2

  (
    cd "$source_dir"
    find . -type f | LC_ALL=C sort | while IFS= read -r file; do
      rel_path=${file#./}
      sha=$(sha256sum "$rel_path" | awk '{print $1}')
      printf '%s\t%s\n' "$sha" "$rel_path"
    done > "$manifest_file"
  )
}

write_manifest_from_files() {
  local base_dir=$1
  local manifest_file=$2
  shift 2

  (
    cd "$base_dir"
    for file in "$@"; do
      rel_path=${file#./}
      sha=$(sha256sum "$rel_path" | awk '{print $1}')
      printf '%s\t%s\n' "$sha" "$rel_path"
    done > "$manifest_file"
  )
}

stage_transfer_dir() {
  local source_dir=$1
  local transfer_name=$2
  local transfer_dir="${TRANSFER_ROOT%/}/${transfer_name}"
  local manifest_tmp

  [[ -d "$source_dir" ]] || fail "Source directory does not exist: $source_dir"

  ensure_dir "$transfer_dir"
  manifest_tmp=$(mktemp)
  write_manifest_from_dir "$source_dir" "$manifest_tmp"
  rsync -rt --delete --stats "${source_dir%/}/" "${transfer_dir%/}/"
  rsync -t --stats "$manifest_tmp" "${transfer_dir%/}/.transfer-manifest.tsv"
  rm -f "$manifest_tmp"
  log "Staged directory ${source_dir} -> ${transfer_dir}"
}

stage_transfer_file() {
  local source_file=$1
  local transfer_name=$2
  local transfer_dir="${TRANSFER_ROOT%/}/${transfer_name}"
  local source_dir
  local filename
  local manifest_tmp

  [[ -f "$source_file" ]] || fail "Source file does not exist: $source_file"

  ensure_dir "$transfer_dir"
  source_dir=$(dirname "$source_file")
  filename=$(basename "$source_file")
  manifest_tmp=$(mktemp)
  write_manifest_from_files "$source_dir" "$manifest_tmp" "$filename"
  rsync -t --stats "$source_file" "${transfer_dir%/}/"
  rsync -t --stats "$manifest_tmp" "${transfer_dir%/}/.transfer-manifest.tsv"
  rm -f "$manifest_tmp"
  log "Staged file ${source_file} -> ${transfer_dir}"
}

stage_transfer_files() {
  local transfer_name=$1
  shift
  local transfer_dir="${TRANSFER_ROOT%/}/${transfer_name}"
  local first_file
  local base_dir
  local relative_files=()
  local manifest_tmp
  local file

  ensure_dir "$transfer_dir"
  first_file=$1
  base_dir=$(dirname "$first_file")
  manifest_tmp=$(mktemp)
  for file in "$@"; do
    relative_files+=("$(basename "$file")")
  done
  write_manifest_from_files "$base_dir" "$manifest_tmp" "${relative_files[@]}"
  rsync -t --stats "$@" "${transfer_dir%/}/"
  rsync -t --stats "$manifest_tmp" "${transfer_dir%/}/.transfer-manifest.tsv"
  rm -f "$manifest_tmp"
  log "Staged files into ${transfer_dir}"
}

run_oras() {
  podman run --rm \
    -v "${HOME}/.docker/config.json:/root/.docker/config.json:Z" \
    -v "$(pwd):/workspace:Z" \
    "${ORAS_IMAGE}" "$@"
}
