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
log "writting manifest file into $manifest_file"
  (
    cd "$source_dir"
    find . -type f | LC_ALL=C sort | while IFS= read -r file; do
      rel_path=${file#./}
      sha=1
      #sha=$(sha256sum "$rel_path" | awk '{print $1}')
      printf '%s\t%s\n' "$sha" "$rel_path"
    done > "$manifest_file"
  )

  # Create staged manifest file
  local staged_manifest_file="${manifest_file%.tsv}-staged.tsv"
  cp "$manifest_file" "$staged_manifest_file"

  # Create last manifest file if it doesn't exist
  local last_manifest_file="${manifest_file%.tsv}-last.tsv"
  if [[ ! -f "$last_manifest_file" ]]; then
    touch "$last_manifest_file"
  fi
}

get_changed_manifest() {
  local staged_manifest_file=$1
  local last_manifest_file=$2
  local temp_file=$3
  log "$staged_manifest_file $last_manifest_file $temp_file"
  # Find files in staged manifest that are not in last manifest
  comm -13 <(sort "$last_manifest_file") <(sort "$staged_manifest_file") > "$temp_file"

  # Update staged manifest with files that are in last manifest but not in staged manifest
  comm -23 <(sort "$last_manifest_file") <(sort "$staged_manifest_file") | while IFS= read -r line; do
    echo "$line delete" >> "$staged_manifest_file"
  done
}

transfer_dir() {
  local source_dir=$1
  local transfer_name=$2
  local transfer_dir="${TRANSFER_ROOT%/}/${transfer_name}"
  local manifest_tmp
  local staged_manifest_file
  local last_manifest_file

  [[ -d "$source_dir" ]] || fail "Source directory does not exist: $source_dir"

  ensure_dir "$transfer_dir"
  manifest_tmp=$(mktemp)
  staged_manifest_file="${source_dir%/}/.transfer-manifest-staged.tsv"
  last_manifest_file="${source_dir%/}/.transfer-manifest-last.tsv"

  #write_manifest_from_dir "$source_dir" "$staged_manifest_file"

  # Get changed manifest
  #get_changed_manifest "$staged_manifest_file" "$last_manifest_file" "$manifest_tmp"

  # Transfer files
  rsync -rtvh \
  --link-dest="${source_dir%/}/" \
  --no-acls \
  --no-xattrs \
  --ignore-missing-args \
  --ignore-errors \
  --info=progress2 \
  "${source_dir%/}/" "${transfer_dir%/}/"

  #--files-from="$manifest_tmp" \

  # Update last manifest file
  #mv "$staged_manifest_file" "$last_manifest_file"

  log "moved directory ${source_dir} -> ${transfer_dir}"
}


run_oras() {
  podman run --rm \
    -v "${HOME}/.docker/config.json:/root/.docker/config.json:Z" \
    -v "$(pwd):/workspace:Z" \
    "${ORAS_IMAGE}" "$@"
}

load_common_config "$COMMON_DIR"
