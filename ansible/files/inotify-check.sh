#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

WATCH_ROOT="${DATADIODE_STAGE_ROOT}/repository"
PROBE_DIR="${WATCH_ROOT}/.ansible-inotify-probe"
EVENT_FILE=$(mktemp)

cleanup() {
  rm -f "${PROBE_DIR}/event.txt" "$EVENT_FILE"
  rmdir "$PROBE_DIR" 2>/dev/null || true
}
trap cleanup EXIT

mkdir -p "$PROBE_DIR"

/usr/bin/timeout 10s /usr/bin/inotifywait \
  --quiet \
  --event close_write \
  --format '%e|%w%f' \
  "$PROBE_DIR" >"$EVENT_FILE" &
watch_pid=$!

sleep 1
printf 'probe\n' > "${PROBE_DIR}/event.txt"

wait "$watch_pid"

grep -q '^CLOSE_WRITE' "$EVENT_FILE" || fail "inotify check failed: no CLOSE_WRITE event captured under $WATCH_ROOT"

printf 'inotify check passed for %s\n' "$WATCH_ROOT"
