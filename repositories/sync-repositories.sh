#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# shellcheck source=../lib/common.sh
source "$SCRIPT_DIR/../lib/common.sh"

load_common_config "$SCRIPT_DIR"
load_config_file "$SCRIPT_DIR/config.env"

require_command reposync
require_command gawk
require_command rsync

sync_repos() {
  local destination_root=$1
  shift

  ensure_dir "$destination_root"

  for repo in "$@"; do
    log "Syncing repository ${repo} into ${destination_root}"
    reposync \
      --gpgcheck \
      --newest-only \
      --delete \
      --download-metadata \
      --exclude='*.src,*.nosrc' \
      -p "$destination_root" \
      --remote-time \
      --repoid "$repo" &> log.reposync
  done
}

sync_ol9_repos() {
  require_command podman

  ensure_dir "$OL9_REPOSITORY_ROOT"

  log "Building OL9 repository sync image ${REPOSITORY_SYNC_IMAGE}"
  podman build -t "$REPOSITORY_SYNC_IMAGE" -f "$REPOSITORY_CONTAINERFILE" "$SCRIPT_DIR"

  log "Syncing OL9 repositories into ${OL9_REPOSITORY_ROOT}"
  podman run --rm \
    -e LOCATION="$OL9_REPOSITORY_ROOT" \
    -e REPO_LIST="${OL9_REPOS[*]}" \
    -v "${OL9_REPOSITORY_ROOT}:${OL9_REPOSITORY_ROOT}:z" \
    "$REPOSITORY_SYNC_IMAGE"
}

generate_repo_file() {
  local source_dir=$1
  local repo_file=$2
  local base_url=$3
  local prefix=$4

  (
    cd "$source_dir"
    ls | gawk -v base_url="$base_url" -v prefix="$prefix" '{
      print "[" $1 "]"
      print "baseurl=" base_url "/" $1
      print "name=" prefix "-" $1
      print "enabled=0"
      print "gpgcheck=0"
      print "skip_if_unavailable=True"
      print ""
    }' > "$repo_file"
  )
}

#sync_repos "$OL8_REPOSITORY_ROOT" "${OL8_REPOS[@]}"
sync_ol9_repos

#generate_repo_file "$OL8_REPOSITORY_ROOT" "${TRUNK_ROOT}/repository/OL8.repo" "$OL8_REPO_FILE_BASEURL" "OL8"
generate_repo_file "$OL9_REPOSITORY_ROOT" "${TRUNK_ROOT}/repository/OL9.repo" "$OL9_REPO_FILE_BASEURL" "OL9"

stage_transfer_dir "${TRUNK_ROOT}/repository" "$REPOSITORY_TRANSFER_NAME"
