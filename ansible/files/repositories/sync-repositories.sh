#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

# shellcheck source=../common.sh
source "$SCRIPT_DIR/../common.sh"

load_common_config "$SCRIPT_DIR"
load_config_file "$SCRIPT_DIR/config.env"

require_command reposync
require_command rsync
require_command tar
require_command curl
require_command jq
require_command sha256sum

REPOSITORY_ROOT="$TRUNK_ROOT/$PROJECT_LOCATION"
REPOSITORY_LIST_FILE="$REPOSITORY_ROOT/list"
REPOSITORY_KEYS_DIR="$REPOSITORY_ROOT/keys"
REPOSITORY_KEYS_LIST_FILE="$REPOSITORY_KEYS_DIR/list"

sync_repos() {
  local version_name=$1
  local repo_file="${SCRIPT_DIR}/oracle-linux-${version_name,,}.repo"
  shift 1

  ensure_dir "$REPOSITORY_ROOT/$version_name"

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
      --repoid "$repo"; then
      transfer "$PROJECT_LOCATION" "$version_name/$repo"
    else
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
    urls+=("${REPOSITORY_BASEURL%/}/keys/${key_name}")
  done

  printf '%s' "${urls[*]-}"
}

generate_repo_file() {
  local repo_label=$1
  local repo_name
  local repo_path
  local relative_path
  local source_dir="$REPOSITORY_ROOT/$repo_label"
  local repo_file="$REPOSITORY_ROOT/$repo_label.repo"
  local base_url="$REPOSITORY_BASEURL/$repo_label"
  local section_name
  local display_name

  : > "${repo_file}.new"

  while IFS= read -r repo_path; do
    relative_path=${repo_path#"$source_dir"/}
    relative_path=${relative_path%/repodata/repomd.xml}
    [[ -n "$relative_path" ]] || continue

    repo_name=$relative_path
    section_name=${relative_path//\//-}
    display_name="${section_name}"

    {
      printf '[%s]\n' "$section_name"
      printf 'baseurl=%s/%s\n' "$base_url" "$repo_name"
      printf 'name=%s\n' "$display_name"
      printf 'enabled=0\n'
      printf 'gpgcheck=1\n'
      if [[ -n "${REPOSITORY_GPGKEY_VALUE:-}" ]]; then
        printf 'gpgkey=%s\n' "$REPOSITORY_GPGKEY_VALUE"
      fi
      printf 'skip_if_unavailable=True\n\n'
    } >> "${repo_file}.new"
  done < <(
    find "$source_dir" -type f -path '*/repodata/repomd.xml' | LC_ALL=C sort
  )

  mv "${repo_file}.new" "$repo_file"
  transfer "$PROJECT_LOCATION" "${repo_label}.repo"
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

  transfer "$PROJECT_LOCATION" "list"
  transfer "$PROJECT_LOCATION" "keys/list"
}

import_nisp_isos() {
  local iso_path
  local staging_dir
  local version
  local target_dir
  local target_repo_file
  local -a nisp_key_paths
  local -a nisp_key_names

  shopt -s nullglob
  for iso_path in $NISP_ISO_GLOB; do
    nisp_key_paths=()
    nisp_key_names=()
    log "Inspecting NISP ISO ${iso_path}"
    staging_dir=$(mktemp -d)
    tar -xf "$iso_path" -C "$staging_dir"

    [[ -f "${staging_dir}/nisp.version" ]] || fail "NISP ISO missing nisp.version: ${iso_path}"
    version=$(awk '/MEDIA:/ { print $2; exit }' "${staging_dir}/nisp.version")
    [[ -n "$version" ]] || fail "Unable to derive NISP version from ${iso_path}"

    target_dir="${REPOSITORY_ROOT}/${version}"
    target_repo_file="${REPOSITORY_ROOT}/${version}.repo"

    if [[ -f "$target_repo_file" ]]; then
      log "NISP version ${version} already present, keeping ${iso_path}"
      rm -rf "$staging_dir"
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
    REPOSITORY_GPGKEY_VALUE=$(format_gpgkey_value "${nisp_key_names[@]}")
    generate_repo_file "$version"
    log "Imported NISP version ${version} into ${target_dir}"
    transfer "$PROJECT_LOCATION" "${version}"
    transfer "$PROJECT_LOCATION" "${version}.repo"
  done
  shopt -u nullglob
}

update_grype_database() {
  local grype_db_url="$1"
  local db_dir="$2"
  local temp_dir="$3"

  log "Fetching latest Grype database information..."
  curl -Lo "$temp_dir/latest.json" "$grype_db_url/latest.json"

  local db_filename db_checksum
  db_filename=$(jq -r '.path' "$temp_dir/latest.json")
  db_checksum=$(jq -r '.checksum | sub("^sha256:"; "")' "$temp_dir/latest.json")

  mkdir -p "$REPOSITORY_ROOT/$db_dir"

  if [ -f "$REPOSITORY_ROOT/$db_dir/$db_filename" ]; then
    local existing_checksum
    existing_checksum=$(sha256sum "$REPOSITORY_ROOT/$db_dir/$db_filename" | cut -d' ' -f1)
    if [ "$existing_checksum" = "$db_checksum" ]; then
      log "Grype database is already up to date"
      return 0
    fi
  fi

  curl -Lo "$temp_dir/$db_filename" "$grype_db_url/$db_filename"

  local downloaded_checksum
  downloaded_checksum=$(sha256sum "$temp_dir/$db_filename" | cut -d' ' -f1)
  [ "$downloaded_checksum" = "$db_checksum" ] || fail "Grype checksum verification failed"

  mv "$temp_dir/$db_filename" "$REPOSITORY_ROOT/$db_dir/$db_filename"
  transfer "$PROJECT_LOCATION" "$db_dir/$db_filename"
}

main() {
  ensure_dir "$REPOSITORY_ROOT"
  ensure_dir "$REPOSITORY_KEYS_DIR"

  local -a key_names=()
  if [[ ${#REPOSITORY_GPG_KEY_FILES[@]} -gt 0 ]]; then
    while IFS= read -r key_name; do
      key_names+=("$key_name")
    done < <(copy_key_files "$REPOSITORY_KEYS_DIR" "${REPOSITORY_GPG_KEY_FILES[@]}")
  fi
  REPOSITORY_GPGKEY_VALUE=$(format_gpgkey_value "${key_names[@]}")

  sync_repos "OL8" "${OL8_REPOS[@]}"
  sync_repos "OL9" "${OL9_REPOS[@]}"
  generate_repo_file "OL8"
  generate_repo_file "OL9"
  import_nisp_isos

  local grype_tmp
  grype_tmp=$(mktemp -d)
  update_grype_database "$GRYPE_DB_URL" "grype/v6" "$grype_tmp"
  rm -rf "$grype_tmp"

  refresh_repo_indexes
}

main "$@"
