#!/usr/bin/env bash

set -euo pipefail

LOG_FILE=${1:-/var/log/datadiode/inotify.log}
shift || true

if [[ $# -eq 0 ]]; then
  echo "Usage: $0 [log_file] <path> [path...]" >&2
  exit 1
fi

mkdir -p "$(dirname "$LOG_FILE")"
touch "$LOG_FILE"

inotifywait \
  --monitor \
  --recursive \
  --event create,modify,delete,move,attrib \
  --timefmt '%Y-%m-%d %H:%M:%S' \
  --format '%T|%e|%w%f' \
  "$@" >>"$LOG_FILE"
