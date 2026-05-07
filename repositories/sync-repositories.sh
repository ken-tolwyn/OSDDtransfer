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

REPOSITORY_ROOT="$TRUNK_ROOT/$PROJECT_LOCATION"
REPOSITORY_LIST_FILE="$REPOSITORY_ROOT/list"
REPOSITORY_KEYS_LIST_FILE="$REPOSITORY_ROOT/keys/list"



# Synchronize multiple repositories for a specific version
#
# This function synchronizes multiple YUM/DNF repositories using reposync,
# downloading package metadata and RPMs to a local directory structure.
#
# @param $1 version_name - The version name (e.g., "OL8", "OL9")
# @param $2 repo_file - Path to the repository configuration file
# @param $@ repos - List of repository IDs to sync

sync_repos() {
  local version_name=$1
  local repo_file="${SCRIPT_DIR}/oracle-linux-${version_name}.repo"
  shift 1

  # Ensure the target directory exists
  ensure_dir "$REPOSITORY_ROOT/$version_name"

  # Sync each repository in the list
  for repo in "$@"; do
    log "Syncing repository ${repo} into $REPOSITORY_ROOT/$version_name"
    if reposync \
      --gpgcheck \
      --newest-only \
      --delete \
      --download-metadata \
      -c "$repo_file" \
      --exclude='*.src,*.nosrc' \
      -p "$REPOSITORY_ROOT/$version_name" \
      --remote-time \
      --repoid "$repo" &> log.reposync; then
      # Transfer the synced repository to the target location
      transfer "$PROJECT_LOCATION" "$version_name/$repo"
    else
      log "Failed to sync repository ${repo}"
    fi
  done
}

# Copy GPG key files to destination directory
# Copies GPG key files from source locations to a destination directory,
# returning the list of successfully copied key filenames.
# Parameters:
#   $1 - destination_dir: Directory where keys should be copied
#   $@ - key_paths: List of GPG key file paths to copy
# Returns:
#   Prints sorted list of copied key filenames to stdout
copy_key_files() {
  local destination_dir=$1
  shift
  local key_path
  local copied=()

  # Ensure destination directory exists
  ensure_dir "$destination_dir"

  # Copy each key file that exists
  for key_path in "$@"; do
    [[ -f "$key_path" ]] || continue
    cp -f "$key_path" "$destination_dir/"
    copied+=("$(basename "$key_path")")
  done

  # Output sorted list of copied keys
  if [[ ${#copied[@]} -gt 0 ]]; then
    printf '%s\n' "${copied[@]}" | LC_ALL=C sort -u
  fi
}

# Format GPG key URLs for repository configuration
# Creates URLs for GPG keys that can be used in repository .repo files.
# Parameters:
#   $@ - key_names: List of GPG key filenames
# Returns:
#   Prints space-separated list of full URLs to the keys
format_gpgkey_value() {
  local key_name
  local urls=()

  # Build URLs for each key
  for key_name in "$@"; do
    [[ -n "$key_name" ]] || continue
    urls+=("${REPOSITORY_BASEURL%/}/keys/${key_name}")
  done

  # Output the URLs as a space-separated string
  printf '%s' "${urls[*]-}"
}

# Generate repository configuration file
# Creates a .repo file that can be used by YUM/DNF clients to access
# the synchronized repositories.
# Parameters:
#   $1 - repo_name: Name of the repository (e.g., "OL8", "OL9")
# source_dir: Directory containing the synced repository data
# repo_file: Output path for the generated .repo file
# base_url: Base URL where the repository will be accessible
# name_prefix: Prefix for repository section names
# section_prefix: Optional prefix for section names
# gpgkey_value: GPG key URL(s) for package verification
generate_repo_file() {
  local repo_name=$1
  local source_dir="$REPOSITORY_ROOT/$repo_name"
  local repo_file="$REPOSITORY_ROOT/$repo_name.repo"
  local base_url="$REPOSITORY_BASEURL/$repo_name"
  local name_prefix="$repo_name"
  local section_prefix=""
  local gpgkey_value=""
  local section_name
  local display_name

  # Start with empty file
  : > "${repo_file}.new"

  # Process each subdirectory in the source directory
  while IFS= read -r repo_name; do
    # Skip if repository doesn't have proper metadata
    [[ -f "${source_dir}/${repo_name}/repodata/repomd.xml" ]] || continue

    # Set section and display names
    section_name=$repo_name
    display_name="${name_prefix}-${repo_name}"

    # Apply section prefix if provided
    if [[ -n "$section_prefix" ]]; then
      section_name="${section_prefix}-${repo_name}"
      display_name=$section_name
    fi

    # Write repository configuration section
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
    # Find all subdirectories and sort them
    find "$source_dir" -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | LC_ALL=C sort
  )

  # Replace old file with new one
  mv "${repo_file}.new" "$repo_file"
  transfer "$PROJECT_LOCATION" "${repo_name}.repo"
  

}

# Refresh repository indexes
# Updates the list files that track available repositories and GPG keys.
# This creates index files used by other systems to discover available content.
refresh_repo_indexes() {
  # Ensure keys directory exists
  ensure_dir "$REPOSITORY_KEYS_DIR"

  # Create list of available repository files
  (
    cd "$REPOSITORY_ROOT"
    find . -maxdepth 1 -type f -name '*.repo' -printf '%f\n' | LC_ALL=C sort > "$REPOSITORY_LIST_FILE"
  )

  # Create list of available GPG key files
  (
    cd "$REPOSITORY_KEYS_DIR"
    find . -maxdepth 1 -type f ! -name 'list' -printf '%f\n' | LC_ALL=C sort > "$REPOSITORY_KEYS_LIST_FILE"
  )
  # Create hard link to the repository list file
  transfer "$PROJECT_LOCATION" "list"
  transfer "$PROJECT_LOCATION" "keys/list"
  
}

# Import NISP ISO files
# Processes NISP ISO files,
# extracting their contents and integrating them into the repository structure.
# This function is currently disabled in the main script.
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

    # Verify ISO contains version information
    [[ -f "${staging_dir}/nisp.version" ]] || fail "NISP ISO missing nisp.version: ${iso_path}"
    version=$(awk '/MEDIA:/ { print $2; exit }' "${staging_dir}/nisp.version")
    [[ -n "$version" ]] || fail "Unable to derive NISP version from ${iso_path}"

    target_dir="${REPOSITORY_ROOT}/${version}"
    target_repo_file="${REPOSITORY_ROOT}/${version}.repo"

    # Skip if this version already exists
    if [[ -f "$target_repo_file" ]]; then
      log "NISP version ${version} already present, removing ${iso_path}"
      rm -rf "$staging_dir"
      rm -f "$iso_path"
      continue
    fi

    # Ensure target directory doesn't already exist
    [[ ! -e "$target_dir" ]] || fail "Target directory already exists for NISP version ${version}: ${target_dir}"

    # Find and process GPG keys from the ISO
    while IFS= read -r key_path; do
      nisp_key_paths+=("$key_path")
    done < <(find "$staging_dir" -maxdepth 1 -type f -name '*-GPG-KEY*' | LC_ALL=C sort)

    # Copy keys to the repository keys directory
    if [[ ${#nisp_key_paths[@]} -gt 0 ]]; then
      while IFS= read -r key_name; do
        nisp_key_names+=("$key_name")
      done < <(copy_key_files "$REPOSITORY_KEYS_DIR" "${nisp_key_paths[@]}")
    fi

    # Move extracted content to final location
    mv "$staging_dir" "$target_dir"
    base_url="${REPOSITORY_BASEURL%/}/${version}"
    gpgkey_value=$(format_gpgkey_value "${nisp_key_names[@]}")

    # Generate repository configuration file
    generate_repo_file "$version"
    rm -f "$iso_path"
    log "Imported NISP version ${version} into ${target_dir}"
    transfer "$PROJECT_LOCATION" "${version}"
    transfer "$PROJECT_LOCATION" "${version}.repo"
    
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
    mkdir -p "$REPOSITORY_ROOT/$db_dir"

    # Check if we already have this version
    if [ -f "$REPOSITORY_ROOT/$db_dir/$db_filename" ]; then
        local existing_checksum
        existing_checksum=$(sha256sum "$REPOSITORY_ROOT/$db_dir/$db_filename" | cut -d' ' -f1)
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
    mv "$temp_dir/$db_filename" "$temp_dir/latest.json" "$REPOSITORY_ROOT/$db_dir/"
    chmod -R a+rX "$REPOSITORY_ROOT/$db_dir"
    transfer "$PROJECT_LOCATION" "$db_dir"
    log "Grype vulnerability database updated successfully!"
    
}

# Main script execution

# Ensure required directories exist
ensure_dir "$REPOSITORY_ROOT"
ensure_dir "$REPOSITORY_ROOT/keys"

# Grype vulnerability database setup
# Grype is a vulnerability scanner for container images and filesystems
ensure_dir "$REPOSITORY_ROOT/grype/v6"
GRYPE_TEMP_DIR=$(mktemp -d)
trap 'rm -rf "$GRYPE_TEMP_DIR"' EXIT
update_grype_database "https://grype.anchore.io/databases/v6" "grype/v6" "$GRYPE_TEMP_DIR"

# Synchronize Oracle Linux repositories
# OL8 = Oracle Linux 8 repositories
# OL9 = Oracle Linux 9 repositories
log "Synchronizing Oracle Linux repositories..."
sync_repos "OL8" "${OL8_REPOS[@]}"
sync_repos "OL9" "${OL9_REPOS[@]}"

# Process GPG keys for the repositories
log "Processing GPG keys..."
mapfile -t repository_key_names < <(copy_key_files "$REPOSITORY_KEYS_DIR" "${REPOSITORY_GPG_KEY_FILES[@]}")
repository_gpgkey_value=$(format_gpgkey_value "${repository_key_names[@]}")

# Generate repository configuration files for clients
log "Generating repository configuration files..."
generate_repo_file "OL8"
generate_repo_file "OL9"

# NISP ISO import (currently disabled)
log "importing NISP"
import_nisp_isos

# Repository index refresh (currently disabled)
log "refreshing index"
refresh_repo_indexes
