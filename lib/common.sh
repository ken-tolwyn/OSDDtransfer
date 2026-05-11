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

apply_common_overrides() {
  if [[ -n "${UPSTREAMD_WORKDIR:-}" ]]; then
    TRUNK_ROOT=$UPSTREAMD_WORKDIR
  fi

  if [[ -n "${UPSTREAMD_TRANSFER_ROOT:-}" ]]; then
    TRANSFER_ROOT=$UPSTREAMD_TRANSFER_ROOT
  elif [[ -n "${UPSTREAMD_WORKDIR:-}" ]]; then
    TRANSFER_ROOT="${UPSTREAMD_WORKDIR%/}/transfer"
  fi
}

ensure_dir() {
  mkdir -p "$1"
}

transfer() {
  local project=$1
  local item=$2
  local source_path="${TRUNK_ROOT%/}/${project%/}/${item}"
  local transfer_path="${TRANSFER_ROOT%/}/${project%/}/${item}"

  [[ -e "$source_path" ]] || fail "Source path does not exist: $source_path"

  log "linking $source_path to $transfer_path"
  if [[ -d "$source_path" ]]; then
    mkdir -p "$transfer_path"
    # copy directory contents, avoid nested dir
    cp -alL "${source_path%/}/." "$transfer_path/"
  else
    # file copy
    cp -alL "$source_path" "$transfer_path"
  fi
}

load_common_config "$COMMON_DIR"
apply_common_overrides
