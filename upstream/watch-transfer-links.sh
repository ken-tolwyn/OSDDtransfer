#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# shellcheck source=../lib/common.sh
source "$SCRIPT_DIR/../lib/common.sh"

load_common_config "$SCRIPT_DIR"
load_config_file "$SCRIPT_DIR/config.env"

require_command inotifywait
require_command cp

remove_empty_parents() {
  local dir_path=$1
  local stop_dir=$2

  while [[ "$dir_path" != "$stop_dir" && "$dir_path" != "/" ]]; do
    rmdir "$dir_path" 2>/dev/null || break
    dir_path=$(dirname "$dir_path")
  done
}

sync_file() {
  local source_file=$1
  local target_file=$2

  ensure_dir "$(dirname "$target_file")"

  if [[ -e "$target_file" ]] && [[ "$source_file" -ef "$target_file" ]]; then
    return 0
  fi

  rm -f "$target_file"
  cp -ln -- "$source_file" "$target_file"
}

sync_tree() {
  local source_root=$1
  local transfer_root=$2
  local source_path
  local relative_path

  ensure_dir "$transfer_root"

  while IFS= read -r source_path; do
    relative_path=${source_path#"$source_root"/}

    if [[ -d "$source_path" ]]; then
      ensure_dir "${transfer_root}/${relative_path}"
    elif [[ -f "$source_path" ]]; then
      sync_file "$source_path" "${transfer_root}/${relative_path}"
    fi
  done < <(find "$source_root" -mindepth 1 | LC_ALL=C sort)
}

handle_event() {
  local source_root=$1
  local transfer_root=$2
  local event_path=$3
  local event_types=$4
  local relative_path
  local target_path

  relative_path=${event_path#"$source_root"/}
  target_path="${transfer_root}/${relative_path}"

  if [[ "$event_types" == *"DELETE"* ]] || [[ "$event_types" == *"MOVED_FROM"* ]]; then
    rm -rf -- "$target_path"
    remove_empty_parents "$(dirname "$target_path")" "$transfer_root"
    return 0
  fi

  if [[ -d "$event_path" ]]; then
    ensure_dir "$target_path"
    return 0
  fi

  if [[ -f "$event_path" ]]; then
    sync_file "$event_path" "$target_path"
  fi
}

declare -a watch_dirs=()

for item in "${UPSTREAM_TRANSFER_ITEMS[@]}"; do
  source_dir="${TRUNK_ROOT}/${item}"
  transfer_dir="${TRANSFER_ROOT}/${item}"
  ensure_dir "$source_dir"
  ensure_dir "$transfer_dir"
  sync_tree "$source_dir" "$transfer_dir"
  watch_dirs+=("$source_dir")
done

log "Watching upstream directories for transfer hard-link sync"

inotifywait -m -r \
  --event "$UPSTREAM_WATCH_EVENTS" \
  --format '%w|%f|%e' \
  "${watch_dirs[@]}" | while IFS='|' read -r watched_dir changed_name changed_events; do
  full_path="${watched_dir%/}/${changed_name}"

  for item in "${UPSTREAM_TRANSFER_ITEMS[@]}"; do
    source_dir="${TRUNK_ROOT}/${item}"
    transfer_dir="${TRANSFER_ROOT}/${item}"

    if [[ "$full_path" == "$source_dir" ]] || [[ "$full_path" == "$source_dir/"* ]]; then
      handle_event "$source_dir" "$transfer_dir" "$full_path" "$changed_events"
      break
    fi
  done
done
