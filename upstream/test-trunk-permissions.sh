#!/usr/bin/env bash

set -euo pipefail

WORKDIR=${1:-/trunk}
STATUS=0

log() {
  printf '%s\n' "$*"
}

check_dir() {
  local dir_path=$1

  if [[ ! -d "$dir_path" ]]; then
    log "FAIL missing directory: $dir_path"
    STATUS=1
    return
  fi

  log "INFO directory present: $dir_path"
  stat -c 'INFO mode=%a owner=%U group=%G path=%n' "$dir_path"

  if [[ ! -w "$dir_path" ]]; then
    log "FAIL directory not writable by current user: $dir_path"
    STATUS=1
    return
  fi

  local probe_dir="${dir_path}/.upstreamd-permission-probe.$$"
  if mkdir "$probe_dir" 2>/dev/null; then
    rmdir "$probe_dir"
    log "PASS writable directory: $dir_path"
  else
    log "FAIL could not create subdirectory in: $dir_path"
    STATUS=1
  fi
}

check_same_filesystem() {
  local left=$1
  local right=$2
  local left_device
  local right_device

  if [[ ! -d "$left" || ! -d "$right" ]]; then
    log "SKIP filesystem check, missing path: $left or $right"
    STATUS=1
    return
  fi

  left_device=$(stat -c '%d' "$left")
  right_device=$(stat -c '%d' "$right")

  if [[ "$left_device" == "$right_device" ]]; then
    log "PASS same filesystem: $left <-> $right"
  else
    log "FAIL different filesystems: $left <-> $right"
    log "HINT hard-link promotion needs source and transfer trees on the same filesystem"
    STATUS=1
  fi
}

check_hardlink() {
  local source_dir=$1
  local target_dir=$2
  local source_file="${source_dir}/.upstreamd-link-source.$$"
  local target_file="${target_dir}/.upstreamd-link-target.$$"

  if [[ ! -d "$source_dir" || ! -d "$target_dir" ]]; then
    log "SKIP hard-link check, missing path: $source_dir or $target_dir"
    STATUS=1
    return
  fi

  printf 'probe\n' > "$source_file"

  if ln "$source_file" "$target_file" 2>/dev/null; then
    log "PASS hard-link creation: $source_dir -> $target_dir"
    rm -f "$target_file" "$source_file"
  else
    log "FAIL could not hard-link from $source_dir to $target_dir"
    log "HINT check write permissions and ensure both paths are on the same filesystem"
    rm -f "$target_file" "$source_file"
    STATUS=1
  fi
}

check_dir "$WORKDIR"
check_dir "$WORKDIR/repository"
check_dir "$WORKDIR/registry"
check_dir "$WORKDIR/maven"
check_dir "$WORKDIR/transfer"
check_dir "$WORKDIR/transfer/repository"
check_dir "$WORKDIR/transfer/registry"
check_dir "$WORKDIR/transfer/maven"

check_same_filesystem "$WORKDIR/repository" "$WORKDIR/transfer/repository"
check_same_filesystem "$WORKDIR/registry" "$WORKDIR/transfer/registry"
check_same_filesystem "$WORKDIR/maven" "$WORKDIR/transfer/maven"

check_hardlink "$WORKDIR/repository" "$WORKDIR/transfer/repository"
check_hardlink "$WORKDIR/registry" "$WORKDIR/transfer/registry"
check_hardlink "$WORKDIR/maven" "$WORKDIR/transfer/maven"

if [[ $STATUS -eq 0 ]]; then
  log "PASS trunk permission and hard-link checks succeeded"
else
  log "FAIL trunk permission and hard-link checks failed"
fi

exit $STATUS
