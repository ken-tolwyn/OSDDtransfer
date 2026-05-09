#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# shellcheck source=../lib/common.sh
source "$SCRIPT_DIR/../lib/common.sh"

load_common_config "$SCRIPT_DIR"
load_config_file "$SCRIPT_DIR/config.env"

require_command "$REPOSILITE_JAVA_BIN"

[[ -f "$REPOSILITE_JAR" ]] || fail "Reposilite JAR not found: $REPOSILITE_JAR"

ensure_dir "$MAVEN_STORAGE_DIR"

command=(
  "$REPOSILITE_JAVA_BIN"
  "${REPOSILITE_JAVA_OPTS[@]}"
  -jar
  "$REPOSILITE_JAR"
)

if [[ -n "${REPOSILITE_CONFIGURATION_FILE:-}" ]]; then
  command+=( --config "$REPOSILITE_CONFIGURATION_FILE" )
else
  command+=( --port "$REPOSILITE_PORT" --local-storage "$MAVEN_STORAGE_DIR" )
fi

command+=( "${REPOSILITE_EXTRA_ARGS[@]}" )

log "Starting Reposilite on port ${REPOSILITE_PORT}"
exec "${command[@]}"
