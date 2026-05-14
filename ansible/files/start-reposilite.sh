#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

if [[ -f "$DATADIODE_REPOSILITE_CONFIG_PATH" ]]; then
  exec "$DATADIODE_REPOSILITE_JAVA_BIN" -jar "$DATADIODE_REPOSILITE_JAR_PATH" \
    --config "$DATADIODE_REPOSILITE_CONFIG_PATH"
fi

exec "$DATADIODE_REPOSILITE_JAVA_BIN" -jar "$DATADIODE_REPOSILITE_JAR_PATH" \
  --port "$DATADIODE_REPOSILITE_PORT" \
  --local-storage "$DATADIODE_REPOSILITE_STORAGE_DIR"
