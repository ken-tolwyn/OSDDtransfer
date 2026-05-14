#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# shellcheck source=common.sh
source "${SCRIPT_DIR}/common.sh"

for dir in "$DATADIODE_TRUNK_ROOT" "$DATADIODE_STAGE_ROOT" "$DATADIODE_TRANSFER_ROOT" "$DATADIODE_ISO_DIR"; do
  [[ -d "$dir" ]] || fail "inode check failed: missing directory $dir"
  [[ -w "$dir" ]] || fail "inode check failed: directory not writable $dir"
done

for item in "${DATADIODE_TRANSFER_ITEMS[@]}"; do
  src="${DATADIODE_STAGE_ROOT}/${item}"
  dst="${DATADIODE_TRANSFER_ROOT}/${item}"
  [[ -d "$src" ]] || fail "inode check failed: missing source directory $src"
  [[ -d "$dst" ]] || fail "inode check failed: missing transfer directory $dst"
  [[ -w "$src" ]] || fail "inode check failed: source directory not writable $src"
  [[ -w "$dst" ]] || fail "inode check failed: transfer directory not writable $dst"

  probe_src="${src}/.ansible-inode-source.$$"
  probe_dst="${dst}/.ansible-inode-target.$$"
  printf 'probe\n' > "$probe_src"
  ln "$probe_src" "$probe_dst" || fail "inode check failed: hard-link failed between $src and $dst"

  src_inode=$(stat -c %i "$probe_src")
  dst_inode=$(stat -c %i "$probe_dst")
  [[ "$src_inode" == "$dst_inode" ]] || fail "inode check failed: inode mismatch between $src and $dst"

  rm -f "$probe_dst" "$probe_src"
done

printf 'inode check passed for %s -> %s\n' "$DATADIODE_STAGE_ROOT" "$DATADIODE_TRANSFER_ROOT"
