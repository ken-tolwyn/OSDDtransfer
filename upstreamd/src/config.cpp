#include "config.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace upstreamd {
namespace {

std::string trim(std::string value) {
  const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };

  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), not_space));
  value.erase(
      std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
  return value;
}

std::string unquote(std::string value) {
  value = trim(std::move(value));
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

bool parse_bool(const std::string& value) {
  if (value == "true") {
    return true;
  }
  if (value == "false") {
    return false;
  }
  throw std::runtime_error("invalid boolean value: " + value);
}

std::vector<std::string> parse_string_array(const std::string& value) {
  std::string raw = trim(value);
  if (raw.size() < 2 || raw.front() != '[' || raw.back() != ']') {
    throw std::runtime_error("invalid array value: " + value);
  }

  raw = raw.substr(1, raw.size() - 2);
  std::vector<std::string> result;
  std::string current;
  bool in_quotes = false;

  for (char ch : raw) {
    if (ch == '"') {
      in_quotes = !in_quotes;
      current.push_back(ch);
      continue;
    }

    if (ch == ',' && !in_quotes) {
      current = trim(current);
      if (!current.empty()) {
        result.push_back(unquote(current));
      }
      current.clear();
      continue;
    }

    current.push_back(ch);
  }

  current = trim(current);
  if (!current.empty()) {
    result.push_back(unquote(current));
  }

  return result;
}

}  // namespace

Config load_config(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("unable to open config file: " + path.string());
  }

  Config config;
  std::string current_section;
  std::string line;
  std::size_t line_number = 0;

  while (std::getline(input, line)) {
    ++line_number;
    line = trim(line);

    if (line.empty() || line.front() == '#') {
      continue;
    }

    if (line.front() == '[' && line.back() == ']') {
      current_section = line.substr(1, line.size() - 2);
      continue;
    }

    const auto equals_pos = line.find('=');
    if (equals_pos == std::string::npos) {
      throw std::runtime_error("invalid config line " +
                               std::to_string(line_number));
    }

    std::string key = trim(line.substr(0, equals_pos));
    std::string value = trim(line.substr(equals_pos + 1));

    if (current_section.empty()) {
      if (key == "workdir") {
        config.workdir = unquote(value);
      } else if (key == "directory_mode") {
        config.directory_mode = unquote(value);
      }
      continue;
    }

    if (current_section == "transfer" && key == "watched_areas") {
      config.watched_areas = parse_string_array(value);
      continue;
    }

    if (current_section == "layout.config_inputs") {
      if (key == "repository_repo_files_dir") {
        config.inputs.repository_repo_files_dir = unquote(value);
      } else if (key == "repository_iso_dir") {
        config.inputs.repository_iso_dir = unquote(value);
      } else if (key == "registry_images_yaml") {
        config.inputs.registry_images_yaml = unquote(value);
      } else if (key == "registry_charts_yaml") {
        config.inputs.registry_charts_yaml = unquote(value);
      } else if (key == "maven_reposilite_config") {
        config.inputs.maven_reposilite_config = unquote(value);
      }
      continue;
    }

    if (current_section == "sync.repositories") {
      if (key == "enabled") {
        config.repository_sync.enabled = parse_bool(value);
      } else if (key == "schedule") {
        config.repository_sync.schedule = unquote(value);
      } else if (key == "full_sync") {
        config.repository_sync.full_sync = unquote(value);
      } else if (key == "repo_files_dir") {
        config.repository_sync.repo_files_dir = unquote(value);
      } else if (key == "iso_dir") {
        config.repository_sync.iso_dir = unquote(value);
      }
      continue;
    }

    if (current_section == "sync.registries") {
      if (key == "enabled") {
        config.registry_sync.enabled = parse_bool(value);
      } else if (key == "schedule") {
        config.registry_sync.schedule = unquote(value);
      } else if (key == "full_sync") {
        config.registry_sync.full_sync = unquote(value);
      } else if (key == "images_yaml") {
        config.registry_sync.images_yaml = unquote(value);
      } else if (key == "charts_yaml") {
        config.registry_sync.charts_yaml = unquote(value);
      } else if (key == "namespace") {
        config.registry_sync.registry_namespace = unquote(value);
      }
      continue;
    }

    if (current_section == "services.zot") {
      if (key == "enabled") {
        config.zot.enabled = parse_bool(value);
      } else if (key == "binary") {
        config.zot.binary = unquote(value);
      } else if (key == "args") {
        config.zot.args = parse_string_array(value);
      } else if (key == "listen_host") {
        config.zot.listen_host = unquote(value);
      } else if (key == "listen_port") {
        config.zot.listen_port = std::stoi(value);
      } else if (key == "data_dir") {
        config.zot.data_dir = unquote(value);
      } else if (key == "runtime_config") {
        config.zot.runtime_config = unquote(value);
      } else if (key == "log_level") {
        config.zot.log_level = unquote(value);
      }
      continue;
    }

    if (current_section == "services.reposilite") {
      if (key == "enabled") {
        config.reposilite.enabled = parse_bool(value);
      } else if (key == "binary") {
        config.reposilite.binary = unquote(value);
      } else if (key == "args") {
        config.reposilite.args = parse_string_array(value);
      }
      continue;
    }
  }

  config.transfer_root = config.workdir / "transfer";
  config.config_root = config.workdir / "config";
  if (config.inputs.repository_repo_files_dir.empty()) {
    config.inputs.repository_repo_files_dir =
        config.config_root / "repositories" / "repo-files";
  }
  if (config.inputs.repository_iso_dir.empty()) {
    config.inputs.repository_iso_dir = config.config_root / "repositories" / "iso";
  }
  if (config.inputs.registry_images_yaml.empty()) {
    config.inputs.registry_images_yaml = config.config_root / "registries" / "images.yaml";
  }
  if (config.inputs.registry_charts_yaml.empty()) {
    config.inputs.registry_charts_yaml = config.config_root / "registries" / "charts.yaml";
  }
  if (config.inputs.maven_reposilite_config.empty()) {
    config.inputs.maven_reposilite_config = config.config_root / "maven" / "reposilite.json";
  }

  if (config.repository_sync.repo_files_dir.empty()) {
    config.repository_sync.repo_files_dir = config.inputs.repository_repo_files_dir;
  }
  if (config.repository_sync.iso_dir.empty()) {
    config.repository_sync.iso_dir = config.inputs.repository_iso_dir;
  }
  if (config.registry_sync.images_yaml.empty()) {
    config.registry_sync.images_yaml = config.inputs.registry_images_yaml;
  }
  if (config.registry_sync.charts_yaml.empty()) {
    config.registry_sync.charts_yaml = config.inputs.registry_charts_yaml;
  }
  if (config.registry_sync.registry_namespace.empty()) {
    config.registry_sync.registry_namespace = "internet";
  }

  if (config.zot.data_dir.empty()) {
    config.zot.data_dir = config.workdir / "registry" / "data";
  }
  if (config.zot.runtime_config.empty()) {
    config.zot.runtime_config = config.workdir / "registry" / "zot-config.generated.json";
  }
  return config;
}

}  // namespace upstreamd
