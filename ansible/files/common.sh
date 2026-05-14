#!/usr/bin/env bash
set -euo pipefail

DATADIODE_ENV=${DATADIODE_ENV:-${HOME}/.config/datadiode-sync/datadiode.env}

if [[ ! -f "$DATADIODE_ENV" ]]; then
  printf 'missing datadiode env file: %s\n' "$DATADIODE_ENV" >&2
  exit 1
fi

# shellcheck disable=SC1090
source "$DATADIODE_ENV"

log() {
  printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"
}

fail() {
  printf '%s\n' "$1" >&2
  exit 1
}

ensure_dir() {
  install -d -m "${DATADIODE_DIR_MODE}" "$1"
}
