#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

exec /usr/bin/podman run \
  --name "$DATADIODE_ZOT_CONTAINER_NAME" \
  --rm \
  -p "${DATADIODE_ZOT_PORT}:${DATADIODE_ZOT_PORT}" \
  -v "${DATADIODE_ZOT_DATA_DIR}:/var/lib/zot:Z" \
  -v "${DATADIODE_ZOT_CONFIG_PATH}:/etc/zot/config.json:ro,Z" \
  "$DATADIODE_ZOT_IMAGE" \
  serve /etc/zot/config.json
