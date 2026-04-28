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

transfer_dir() {
  local source_dir=$1
  local transfer_name=$2
  local transfer_dir="${TRANSFER_ROOT%/}/${transfer_name}"
  local manifest_tmp

  [[ -d "$source_dir" ]] || fail "Source directory does not exist: $source_dir"

  ensure_dir "$transfer_dir"
  manifest_tmp=$(mktemp)
  write_manifest_from_dir "$source_dir" "$manifest_tmp"

  rsync -rtvh \
  --inplace \
  --no-acls \
  --no-xattrs \
  --ignore-missing-args \
  --ignore-errors \
  --info=progress2 \
  "${source_dir%/}/" "${transfer_dir%/}/"
  install -m 0644 "$manifest_tmp" "${transfer_dir%/}/.transfer-manifest.tsv"
  rm -f "$manifest_tmp"
  log "moved directory ${source_dir} -> ${transfer_dir}"
}


run_oras() {
  podman run --rm \
    -v "${HOME}/.docker/config.json:/root/.docker/config.json:Z" \
    -v "$(pwd):/workspace:Z" \
    "${ORAS_IMAGE}" "$@"
}
