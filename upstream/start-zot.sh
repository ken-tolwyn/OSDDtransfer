#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# shellcheck source=../lib/common.sh
source "$SCRIPT_DIR/../lib/common.sh"

load_common_config "$SCRIPT_DIR"
load_config_file "$SCRIPT_DIR/config.env"

require_command "$UPSTREAM_ZOT_BIN"

ensure_dir "$UPSTREAM_ZOT_DATA_DIR"
ensure_dir "$(dirname "$UPSTREAM_ZOT_RUNTIME_CONFIG")"

cat > "$UPSTREAM_ZOT_RUNTIME_CONFIG" <<EOF
{
  "distSpecVersion": "1.1.1",
  "storage": {
    "rootDirectory": "${UPSTREAM_ZOT_DATA_DIR}"
  },
  "http": {
    "address": "${UPSTREAM_ZOT_HOST}",
    "port": "${UPSTREAM_ZOT_PORT}"
  },
  "log": {
    "level": "${UPSTREAM_ZOT_LOG_LEVEL}"
  }
}
EOF

log "Starting Zot on ${UPSTREAM_ZOT_HOST}:${UPSTREAM_ZOT_PORT}"
exec "$UPSTREAM_ZOT_BIN" serve "$UPSTREAM_ZOT_RUNTIME_CONFIG"
