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
  std::string repository_baseurl{"https://repo.k.mis.local"};
  std::vector<std::filesystem::path> repository_gpg_key_files;
  std::string reposync_binary{"reposync"};
  std::string repository_iso_extractor{"/usr/bin/bsdtar"};
  std::vector<std::string> repository_iso_extractor_args{"-xf"};
  bool grype_enabled{true};
  std::string grype_db_url{"https://grype.anchore.io/databases/v6"};
  std::string grype_db_subdir{"grype/v6"};
  std::string registry_chart_namespace{"internet"};
  std::string registry_host{"localhost"};
  int registry_port{5001};
  std::string yq_binary{"yq"};
  std::string skopeo_binary{"skopeo"};
  std::string curl_binary{"curl"};
  std::string sha256sum_binary{"sha256sum"};
  std::string helm_runner{"podman"};
  std::string helm_container_image{"docker.io/alpine/helm:3.20.2"};
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
  std::filesystem::path config_root{"/config"};
  ConfigInputs inputs;

  ZotService zot;
  ReposiliteService reposilite;
  SyncPolicy repository_sync;
  SyncPolicy registry_sync;
};

Config load_config(const std::filesystem::path& path);
std::vector<std::filesystem::path> discover_config_files(
    const std::filesystem::path& root, const std::string& extension);

}  // namespace upstreamd
