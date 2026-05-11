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

bool is_within_root(const std::filesystem::path& candidate,
                    const std::filesystem::path& root) {
  const auto candidate_text = candidate.lexically_normal().string();
  const auto root_text = root.lexically_normal().string();
  return candidate_text == root_text ||
         candidate_text.rfind(root_text + "/", 0) == 0;
}

}  // namespace

void ensure_layout(const Config& config) {
  const auto perms = parse_mode(config.directory_mode);

  ensure_directory(config.workdir, perms);
  ensure_directory(config.transfer_root, perms);

  for (const auto& area : config.watched_areas) {
    ensure_directory(config.workdir / area, perms);
    ensure_directory(config.transfer_root / area, perms);
  }

  ensure_directory(config.workdir / "registry" / "data", perms);
  ensure_directory(config.workdir / "maven" / "reposilite", perms);
  if (!config.repository_sync.iso_dir.empty() &&
      is_within_root(config.repository_sync.iso_dir, config.workdir)) {
    ensure_directory(config.repository_sync.iso_dir, perms);
  }
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
            << " full=" << config.repository_sync.full_sync;
  if (!config.repository_sync.command.empty()) {
    std::cout << " command=" << config.repository_sync.command.front();
  }
  std::cout << '\n';
  std::cout << "registry sync: "
            << (config.registry_sync.enabled ? "enabled" : "disabled")
            << " schedule=" << config.registry_sync.schedule
            << " full=" << config.registry_sync.full_sync
            << " namespace=" << config.registry_sync.registry_namespace;
  if (!config.registry_sync.command.empty()) {
    std::cout << " command=" << config.registry_sync.command.front();
  }
  std::cout << '\n';
  std::cout << "zot: " << (config.zot.enabled ? "enabled" : "disabled")
            << " data=" << config.zot.data_dir
            << " config=" << config.zot.runtime_config << '\n';
  std::cout << "reposilite: "
            << (config.reposilite.enabled ? "enabled" : "disabled")
            << " binary=" << config.reposilite.binary << '\n';
}

}  // namespace upstreamd
