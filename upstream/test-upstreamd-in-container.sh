#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

IMAGE=${UPSTREAMD_TEST_IMAGE:-container-registry.oracle.com/os/oraclelinux:9-slim}
HOST_TRUNK=${UPSTREAMD_TEST_TRUNK:-/home/ken/trunk}
CONTAINER_TRUNK=${UPSTREAMD_CONTAINER_TRUNK:-/trunk}

if [[ $# -lt 1 ]]; then
  printf 'usage: %s <config-path> [upstreamd args...]\n' "${BASH_SOURCE[0]}" >&2
  exit 2
fi

CONFIG_PATH=$1
shift

case "$CONFIG_PATH" in
  /*) ;;
  *) CONFIG_PATH="/workspace/${CONFIG_PATH}" ;;
esac

ARGS=("$@")
COMMAND=("./upstreamd/bin/upstreamd" "$CONFIG_PATH")
if [[ ${#ARGS[@]} -gt 0 ]]; then
  COMMAND+=("${ARGS[@]}")
fi

exec podman run --rm \
  -v "${REPO_ROOT}:/workspace:Z" \
  -v "${HOST_TRUNK}:${CONTAINER_TRUNK}:Z" \
  -w /workspace \
  "$IMAGE" \
  "${COMMAND[@]}"
