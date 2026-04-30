#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# shellcheck source=lib/common.sh
source "$SCRIPT_DIR/lib/common.sh"

load_common_config "$SCRIPT_DIR"

declare -A JOBS=()

start_job() {
  local name=$1
  shift

  log "Starting ${name}"
  "$@" &
  JOBS["$name"]=$!
}

wait_for_jobs() {
  local rc=0
  local name

  for name in "${!JOBS[@]}"; do
    local pid=${JOBS[$name]}
    if wait "$pid"; then
      log "Completed ${name}"
    else
      log "FAILED ${name}"
      rc=1
    fi
  done

  return "$rc"
}

if [[ "$1" == "--sync" ]]; then
  shift
  for arg in "$@"; do
    case "$arg" in
      repo)
        start_job repositories "$SCRIPT_DIR/repositories/sync-repositories.sh"
        ;;
      registry)
        start_job registries "$SCRIPT_DIR/registries/sync-registries.sh"
        ;;
      nexus)
        if [[ -f "$SCRIPT_DIR/nexus/config.env" ]]; then
          start_job nexus "$SCRIPT_DIR/nexus/export-nexus-backup.sh"
        else
          log "Skipping nexus because nexus/config.env is not present"
        fi
        ;;
      hauler)
        start_job hauler "$SCRIPT_DIR/hauler/sync-hauler.sh"
        ;;
      *)
        log "Unknown sync type: $arg"
        exit 1
        ;;
    esac
  done
else
  start_job repositories "$SCRIPT_DIR/repositories/sync-repositories.sh"
  start_job registries "$SCRIPT_DIR/registries/sync-registries.sh"
  if [[ -f "$SCRIPT_DIR/nexus/config.env" ]]; then
    start_job nexus "$SCRIPT_DIR/nexus/export-nexus-backup.sh"
  else
    log "Skipping nexus because nexus/config.env is not present"
  fi
  start_job hauler "$SCRIPT_DIR/hauler/sync-hauler.sh"
fi
if [[ -f "$SCRIPT_DIR/nexus/config.env" ]]; then
  start_job nexus "$SCRIPT_DIR/nexus/export-nexus-backup.sh"
else
  log "Skipping nexus because nexus/config.env is not present"
fi

wait_for_jobs
