#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace upstreamd {

struct SyncPolicy {
  bool enabled{false};
  std::string schedule;
  std::string full_sync;
  std::vector<std::string> command;
  std::filesystem::path repo_files_dir;
  std::filesystem::path iso_dir;
  std::filesystem::path images_yaml;
  std::filesystem::path charts_yaml;
  std::string registry_namespace;
};

struct ConfigInputs {
  std::filesystem::path repository_repo_files_dir;
  std::filesystem::path repository_iso_dir;
  std::filesystem::path registry_images_yaml;
  std::filesystem::path registry_charts_yaml;
  std::filesystem::path maven_reposilite_config;
};

struct ZotService {
  bool enabled{false};
  std::string binary{"/usr/bin/zot"};
  std::vector<std::string> args;
  std::string listen_host{"0.0.0.0"};
  int listen_port{5001};
  std::filesystem::path data_dir;
  std::filesystem::path runtime_config;
  std::string log_level{"info"};
};

struct ReposiliteService {
  bool enabled{false};
  std::string binary{"/usr/bin/java"};
  std::vector<std::string> args;
};

struct Config {
  std::filesystem::path workdir{"/trunk"};
  std::string directory_mode{"2775"};
  std::vector<std::string> watched_areas{"repository", "registry", "maven"};
  std::filesystem::path transfer_root;
  std::filesystem::path config_root;
  ConfigInputs inputs;

  ZotService zot;
  ReposiliteService reposilite;
  SyncPolicy repository_sync;
  SyncPolicy registry_sync;
};

Config load_config(const std::filesystem::path& path);

}  // namespace upstreamd
