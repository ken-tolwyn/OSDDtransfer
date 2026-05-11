#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

IMAGE=${UPSTREAMD_TEST_IMAGE:-container-registry.oracle.com/os/oraclelinux:9-slim}
HOST_TRUNK=${UPSTREAMD_TEST_TRUNK:-/home/ken/trunk}
HOST_CONFIG=${UPSTREAMD_TEST_CONFIG_DIR:-${REPO_ROOT}/container-config}
CONTAINER_TRUNK=${UPSTREAMD_CONTAINER_TRUNK:-/trunk}
CONTAINER_CONFIG=${UPSTREAMD_CONTAINER_CONFIG:-/config}

if [[ $# -lt 1 ]]; then
  printf 'usage: %s <config-path> [upstreamd args...]\n' "${BASH_SOURCE[0]}" >&2
  exit 2
fi

CONFIG_PATH=$1
shift

case "$CONFIG_PATH" in
  /*) ;;
  *)
    if [[ -f "${HOST_CONFIG}/${CONFIG_PATH}" ]]; then
      CONFIG_PATH="${CONTAINER_CONFIG}/${CONFIG_PATH}"
    else
      CONFIG_PATH="/workspace/${CONFIG_PATH}"
    fi
    ;;
esac

ARGS=("$@")
COMMAND=("./upstreamd/bin/upstreamd" "$CONFIG_PATH")
if [[ ${#ARGS[@]} -gt 0 ]]; then
  COMMAND+=("${ARGS[@]}")
fi

exec podman run --rm \
  -e UPSTREAMD_SYNC_DRY_RUN \
  -e UPSTREAMD_WATCH_DEBUG \
  -v "${REPO_ROOT}:/workspace:Z" \
  -v "${HOST_TRUNK}:${CONTAINER_TRUNK}:Z" \
  -v "${HOST_CONFIG}:${CONTAINER_CONFIG}:Z" \
  -w /workspace \
  "$IMAGE" \
  "${COMMAND[@]}"
