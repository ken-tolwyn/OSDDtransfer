#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# shellcheck source=../lib/common.sh
source "$SCRIPT_DIR/../lib/common.sh"

load_common_config "$SCRIPT_DIR"
load_config_file "$SCRIPT_DIR/config.env"

require_command reposync
require_command rsync
require_command bsdtar

REPOSITORY_LIST_FILE="${REPOSITORY_ROOT}/list"
REPOSITORY_KEYS_LIST_FILE="${REPOSITORY_KEYS_DIR}/list"

sync_repos() {
  local destination_root=$1
  local repo_file=$2
  shift 2

  ensure_dir "$destination_root"

  for repo in "$@"; do
    log "Syncing repository ${repo} into ${destination_root}"
    if ! reposync \
      --gpgcheck \
      --newest-only \
      --delete \
      --download-metadata \
      -c "$repo_file" \
      --exclude='*.src,*.nosrc' \
      -p "$destination_root" \
      --remote-time \
      --repoid "$repo" &> log.reposync; then
      log "Failed to sync repository ${repo}"
    fi
  done
}

copy_key_files() {
  local destination_dir=$1
  shift
  local key_path
  local copied=()

  ensure_dir "$destination_dir"

  for key_path in "$@"; do
    [[ -f "$key_path" ]] || continue
    cp -f "$key_path" "$destination_dir/"
    copied+=("$(basename "$key_path")")
  done

  if [[ ${#copied[@]} -gt 0 ]]; then
    printf '%s\n' "${copied[@]}" | LC_ALL=C sort -u
  fi
}

format_gpgkey_value() {
  local key_name
  local urls=()

  for key_name in "$@"; do
    [[ -n "$key_name" ]] || continue
    urls+=("${REPOSITORY_KEYS_BASEURL%/}/${key_name}")
  done

  printf '%s' "${urls[*]-}"
}

generate_repo_file() {
  local source_dir=$1
  local repo_file=$2
  local base_url=$3
  local name_prefix=$4
  local section_prefix=$5
  local gpgkey_value=$6
  local repo_name
  local section_name
  local display_name

  : > "${repo_file}.new"

  while IFS= read -r repo_name; do
    [[ -f "${source_dir}/${repo_name}/repodata/repomd.xml" ]] || continue

    section_name=$repo_name
    display_name="${name_prefix}-${repo_name}"

    if [[ -n "$section_prefix" ]]; then
      section_name="${section_prefix}-${repo_name}"
      display_name=$section_name
    fi

    {
      printf '[%s]\n' "$section_name"
      printf 'baseurl=%s/%s\n' "$base_url" "$repo_name"
      printf 'name=%s\n' "$display_name"
      printf 'enabled=0\n'
      printf 'gpgcheck=1\n'
      if [[ -n "$gpgkey_value" ]]; then
        printf 'gpgkey=%s\n' "$gpgkey_value"
      fi
      printf 'skip_if_unavailable=True\n\n'
    } >> "${repo_file}.new"
  done < <(
    find "$source_dir" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | LC_ALL=C sort
  )

  mv "${repo_file}.new" "$repo_file"
}

refresh_repo_indexes() {
  ensure_dir "$REPOSITORY_KEYS_DIR"

  (
    cd "$REPOSITORY_ROOT"
    find . -maxdepth 1 -type f -name '*.repo' -printf '%f\n' | LC_ALL=C sort > "$REPOSITORY_LIST_FILE"
  )

  (
    cd "$REPOSITORY_KEYS_DIR"
    find . -maxdepth 1 -type f ! -name 'list' -printf '%f\n' | LC_ALL=C sort > "$REPOSITORY_KEYS_LIST_FILE"
  )
}

import_nisp_isos() {
  local iso_path
  local staging_dir
  local version
  local target_dir
  local target_repo_file
  local base_url
  local gpgkey_value
  local -a nisp_key_paths
  local -a nisp_key_names
  log "for all iso present"
  shopt -s nullglob
  for iso_path in $NISP_ISO_GLOB; do
    nisp_key_paths=()
    nisp_key_names=()
    log "Inspecting NISP ISO ${iso_path}"
    staging_dir=$(mktemp -d)
    bsdtar -xf "$iso_path" -C "$staging_dir"

    [[ -f "${staging_dir}/nisp.version" ]] || fail "NISP ISO missing nisp.version: ${iso_path}"
    version=$(awk '/MEDIA:/ { print $2; exit }' "${staging_dir}/nisp.version")
    [[ -n "$version" ]] || fail "Unable to derive NISP version from ${iso_path}"

    target_dir="${REPOSITORY_ROOT}/${version}"
    target_repo_file="${REPOSITORY_ROOT}/${version}.repo"

    if [[ -f "$target_repo_file" ]]; then
      log "NISP version ${version} already present, removing ${iso_path}"
      rm -rf "$staging_dir"
      rm -f "$iso_path"
      continue
    fi

    [[ ! -e "$target_dir" ]] || fail "Target directory already exists for NISP version ${version}: ${target_dir}"

    while IFS= read -r key_path; do
      nisp_key_paths+=("$key_path")
    done < <(find "$staging_dir" -maxdepth 1 -type f -name '*-GPG-KEY*' | LC_ALL=C sort)

    if [[ ${#nisp_key_paths[@]} -gt 0 ]]; then
      while IFS= read -r key_name; do
        nisp_key_names+=("$key_name")
      done < <(copy_key_files "$REPOSITORY_KEYS_DIR" "${nisp_key_paths[@]}")
    fi

    mv "$staging_dir" "$target_dir"
    base_url="${REPOSITORY_BASEURL%/}/${version}"
    gpgkey_value=$(format_gpgkey_value "${nisp_key_names[@]}")

    generate_repo_file "$target_dir" "$target_repo_file" "$base_url" "$version" "$version" "$gpgkey_value"
    rm -f "$iso_path"
    log "Imported NISP version ${version} into ${target_dir}"
  done
  shopt -u nullglob
}


# update_grype_database <grype_db_url> <db_dir> <temp_dir>
#
#   grype_db_url  - Base URL for the Grype database, e.g. https://grype.anchore.io/databases/v6
#   db_dir        - Destination directory where the database files are stored
#   temp_dir      - Scratch directory used for downloads and verification
update_grype_database() {
    local grype_db_url="$1"
    local db_dir="$2"
    local temp_dir="$3"

    log "Fetching latest Grype database information..."

    # Get latest database info
    curl -Lo "$temp_dir/latest.json" "$grype_db_url/latest.json"

    # Parse database information
    local db_filename db_checksum db_built
    db_filename=$(jq -r '.path' "$temp_dir/latest.json")
    db_checksum=$(jq -r '.checksum | sub("^sha256:"; "")' "$temp_dir/latest.json")
    db_built=$(jq -r '.built' "$temp_dir/latest.json")

    log "Database built: $db_built"
    log "Database checksum: $db_checksum"
    log "Database filename: $db_filename"

    # Create database directory if it doesn't exist
    mkdir -p "$db_dir"

    # Check if we already have this version
    if [ -f "$db_dir/$db_filename" ]; then
        local existing_checksum
        existing_checksum=$(sha256sum "$db_dir/$db_filename" | cut -d' ' -f1)
        if [ "$existing_checksum" = "$db_checksum" ]; then
            log "Database is already up to date"
            return 0
        fi
    fi

    # Download database
    log "Downloading vulnerability database..."
    curl -Lo "$temp_dir/$db_filename" "$grype_db_url/$db_filename"

    # Verify checksum
    log "Verifying checksum..."
    local downloaded_checksum
    downloaded_checksum=$(sha256sum "$temp_dir/$db_filename" | cut -d' ' -f1)

    if [ "$downloaded_checksum" != "$db_checksum" ]; then
        log "ERROR: Checksum verification failed!"
        log "Expected: $db_checksum"
        log "Got: $downloaded_checksum"
        return 1
    fi

    log "Checksum verified successfully"

    # Move database to shared directory
    mv "$temp_dir/$db_filename" "$temp_dir/latest.json" "$db_dir/"
    chmod -R a+rX "$db_dir"

    log "Grype vulnerability database updated successfully!"
}

ensure_dir "$REPOSITORY_ROOT"
ensure_dir "$REPOSITORY_KEYS_DIR"

# Grype vulnerability database
ensure_dir "$REPOSITORY_ROOT/grype"
GRYPE_TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$GRYPE_TEMP_DIR"' EXIT
update_grype_database "https://grype.anchore.io/databases/v6" "$REPOSITORY_ROOT/grype" "$GRYPE_TEMP_DIR"

sync_repos "$OL8_REPOSITORY_ROOT" "${SCRIPT_DIR}/oracle-linux-ol8.repo" "${OL8_REPOS[@]}"
sync_repos "$OL9_REPOSITORY_ROOT" "${SCRIPT_DIR}/oracle-linux-ol9.repo" "${OL9_REPOS[@]}"

mapfile -t repository_key_names < <(copy_key_files "$REPOSITORY_KEYS_DIR" "${REPOSITORY_GPG_KEY_FILES[@]}")
repository_gpgkey_value=$(format_gpgkey_value "${repository_key_names[@]}")

generate_repo_file "$OL8_REPOSITORY_ROOT" "${REPOSITORY_ROOT}/OL8.repo" "$OL8_REPO_FILE_BASEURL" "OL8" "" "$repository_gpgkey_value"
generate_repo_file "$OL9_REPOSITORY_ROOT" "${REPOSITORY_ROOT}/OL9.repo" "$OL9_REPO_FILE_BASEURL" "OL9" "" "$repository_gpgkey_value"
log "importing NISP"
import_nisp_isos
log "refreshing index"
refresh_repo_indexes

transfer_dir "$REPOSITORY_ROOT" "$REPOSITORY_TRANSFER_NAME"
