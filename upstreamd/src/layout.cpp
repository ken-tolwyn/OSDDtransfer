#include "layout.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <system_error>

namespace upstreamd {
namespace {

std::filesystem::perms parse_mode(const std::string& mode_text) {
  const auto mode = std::stoul(mode_text, nullptr, 8);
  return static_cast<std::filesystem::perms>(mode);
}

void ensure_directory(const std::filesystem::path& path,
                      std::filesystem::perms perms) {
  std::error_code error;
  std::filesystem::create_directories(path, error);
  if (error) {
    throw std::runtime_error("failed to create directory " + path.string() +
                             ": " + error.message());
  }

  std::filesystem::permissions(path, perms,
                               std::filesystem::perm_options::replace, error);
  if (error) {
    throw std::runtime_error("failed to set permissions on " + path.string() +
                             ": " + error.message());
  }
}

}  // namespace

void ensure_layout(const Config& config) {
  const auto perms = parse_mode(config.directory_mode);

  ensure_directory(config.workdir, perms);
  ensure_directory(config.transfer_root, perms);
  ensure_directory(config.config_root, perms);

  for (const auto& area : config.watched_areas) {
    ensure_directory(config.workdir / area, perms);
    ensure_directory(config.transfer_root / area, perms);
  }

  ensure_directory(config.workdir / "registry" / "data", perms);
  ensure_directory(config.workdir / "maven" / "reposilite", perms);
  ensure_directory(config.config_root / "repositories" / "repo-files", perms);
  ensure_directory(config.config_root / "repositories" / "iso", perms);
  ensure_directory(config.config_root / "registries", perms);
  ensure_directory(config.config_root / "maven", perms);
}

void print_summary(const Config& config) {
  std::cout << "workdir: " << config.workdir << '\n';
  std::cout << "transfer root: " << config.transfer_root << '\n';
  std::cout << "config root: " << config.config_root << '\n';
  std::cout << "repo input dir: " << config.repository_sync.repo_files_dir << '\n';
  std::cout << "repo iso dir: " << config.repository_sync.iso_dir << '\n';
  std::cout << "registry images yaml: " << config.registry_sync.images_yaml << '\n';
  std::cout << "registry charts yaml: " << config.registry_sync.charts_yaml << '\n';
  std::cout << "watched areas:";
  for (const auto& area : config.watched_areas) {
    std::cout << ' ' << area;
  }
  std::cout << '\n';
  std::cout << "repository sync: "
            << (config.repository_sync.enabled ? "enabled" : "disabled")
            << " schedule=" << config.repository_sync.schedule
            << " full=" << config.repository_sync.full_sync << '\n';
  std::cout << "registry sync: "
            << (config.registry_sync.enabled ? "enabled" : "disabled")
            << " schedule=" << config.registry_sync.schedule
            << " full=" << config.registry_sync.full_sync
            << " namespace=" << config.registry_sync.registry_namespace << '\n';
  std::cout << "zot: " << (config.zot.enabled ? "enabled" : "disabled")
            << " data=" << config.zot.data_dir
            << " config=" << config.zot.runtime_config << '\n';
  std::cout << "reposilite: "
            << (config.reposilite.enabled ? "enabled" : "disabled")
            << " binary=" << config.reposilite.binary << '\n';
}

}  // namespace upstreamd
