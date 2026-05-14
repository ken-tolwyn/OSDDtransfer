#include "registry_sync.hpp"

#include "promote.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace upstreamd {
namespace {

bool dry_run() {
  const char* value = std::getenv("UPSTREAMD_SYNC_DRY_RUN");
  if (value == nullptr) {
    return false;
  }
  const std::string text(value);
  return text == "1" || text == "true";
}

std::vector<char*> make_argv(const std::vector<std::string>& command) {
  std::vector<char*> argv;
  argv.reserve(command.size() + 1);
  for (const auto& part : command) {
    argv.push_back(const_cast<char*>(part.c_str()));
  }
  argv.push_back(nullptr);
  return argv;
}

void run_command(const std::vector<std::string>& command) {
  if (command.empty()) {
    throw std::runtime_error("missing registry helper command");
  }
  const pid_t pid = ::fork();
  if (pid < 0) {
    throw std::runtime_error("failed to fork registry helper command");
  }
  if (pid == 0) {
    auto argv = make_argv(command);
    ::execvp(command.front().c_str(), argv.data());
    _exit(127);
  }
  int status = 0;
  if (::waitpid(pid, &status, 0) < 0) {
    throw std::runtime_error("failed waiting for registry helper command");
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    throw std::runtime_error("registry helper command failed");
  }
}

std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const auto last = value.find_last_not_of(" \t\r\n");
  value = value.substr(first, last - first + 1);
  if ((value.size() >= 2 && value.front() == '"' && value.back() == '"') ||
      (value.size() >= 2 && value.front() == '\'' && value.back() == '\'')) {
    value = value.substr(1, value.size() - 2);
  }
  return value;
}

int indent_of(const std::string& line) {
  int indent = 0;
  while (indent < static_cast<int>(line.size()) && line[indent] == ' ') {
    ++indent;
  }
  return indent;
}

std::vector<std::filesystem::path> config_files_for_extension(
    const std::filesystem::path& source, const std::string& extension) {
  if (std::filesystem::is_directory(source)) {
    return discover_config_files(source, extension);
  }
  if (std::filesystem::is_regular_file(source)) {
    return {source};
  }
  return {};
}

std::string registry_base(const Config& config) {
  return config.registry_sync.registry_host + ":" +
         std::to_string(config.registry_sync.registry_port);
}

std::string registry_image_base(const Config& config) {
  return registry_base(config) + "/" + config.registry_sync.registry_namespace;
}

std::vector<std::string> discover_image_entries(const Config& config) {
  std::vector<std::string> entries;
  for (const auto& file : config_files_for_extension(config.registry_sync.images_yaml,
                                                     ".images")) {
    std::ifstream input(file);
    if (!input) {
      throw std::runtime_error("unable to open images file: " + file.string());
    }

    std::string current_registry;
    std::string current_image;
    std::string line;
    while (std::getline(input, line)) {
      const auto trimmed = trim(line);
      if (trimmed.empty() || trimmed == "---" || trimmed[0] == '#') {
        continue;
      }
      const auto indent = indent_of(line);

      if (indent == 0 && trimmed.back() == ':') {
        current_registry = trim(trimmed.substr(0, trimmed.size() - 1));
        current_image.clear();
        continue;
      }
      if (trimmed == "images:") {
        continue;
      }
      if (indent >= 4 && trimmed.back() == ':' && trimmed[0] != '-') {
        current_image = trim(trimmed.substr(0, trimmed.size() - 1));
        continue;
      }
      if (indent >= 6 && trimmed.rfind("- ", 0) == 0 && !current_registry.empty() &&
          !current_image.empty()) {
        const auto tag = trim(trimmed.substr(2));
        entries.push_back(current_registry + "\t" + current_image + "\t" + tag);
      }
    }
  }
  return entries;
}

struct ChartSpec {
  std::string name;
  std::string repo_url;
  std::string version;
  std::string target_path{"charts"};
};

std::vector<ChartSpec> discover_chart_entries(const Config& config) {
  std::vector<ChartSpec> charts;
  for (const auto& file : config_files_for_extension(config.registry_sync.charts_yaml,
                                                     ".charts")) {
    std::ifstream input(file);
    if (!input) {
      throw std::runtime_error("unable to open charts file: " + file.string());
    }

    ChartSpec current;
    bool in_chart = false;
    std::string line;
    while (std::getline(input, line)) {
      const auto trimmed = trim(line);
      if (trimmed.empty() || trimmed[0] == '#') {
        continue;
      }
      if (trimmed == "charts:") {
        continue;
      }
      if (trimmed.rfind("- ", 0) == 0) {
        if (in_chart && !current.name.empty() && !current.repo_url.empty()) {
          charts.push_back(current);
        }
        current = {};
        current.target_path = "charts";
        in_chart = true;
        const auto remainder = trim(trimmed.substr(2));
        const auto colon = remainder.find(':');
        if (colon != std::string::npos) {
          const auto key = trim(remainder.substr(0, colon));
          const auto value = trim(remainder.substr(colon + 1));
          if (key == "name") {
            current.name = value;
          }
        }
        continue;
      }
      if (!in_chart) {
        continue;
      }
      const auto colon = trimmed.find(':');
      if (colon == std::string::npos) {
        continue;
      }
      const auto key = trim(trimmed.substr(0, colon));
      const auto value = trim(trimmed.substr(colon + 1));
      if (key == "name") {
        current.name = value;
      } else if (key == "repoURL") {
        current.repo_url = value;
      } else if (key == "version") {
        current.version = value;
      } else if (key == "targetPath") {
        current.target_path = value;
      }
    }
    if (in_chart && !current.name.empty() && !current.repo_url.empty()) {
        charts.push_back(current);
    }
  }
  return charts;
}

void touch_marker(const std::filesystem::path& path) {
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("unable to create marker: " + path.string());
  }
  output << "ok\n";
}

void copy_image(const Config& config,
                const std::string& source_ref,
                const std::string& destination_ref) {
  run_command({config.registry_sync.skopeo_binary, "copy", "--dest-tls-verify=false",
               "-q", "docker://" + source_ref, "docker://" + destination_ref});
}

void sync_images(const Config& config) {
  std::size_t count = 0;
  for (const auto& line : discover_image_entries(config)) {
    std::istringstream stream(line);
    std::string registry;
    std::string image;
    std::string tag;
    std::getline(stream, registry, '\t');
    std::getline(stream, image, '\t');
    std::getline(stream, tag, '\t');
    if (registry.empty() || image.empty() || tag.empty()) {
      continue;
    }
    ++count;
    if (!dry_run()) {
      copy_image(config, registry + "/" + image + ":" + tag,
                 registry_image_base(config) + "/" + image + ":" + tag);
    }
  }
  if (dry_run()) {
    touch_marker(config.workdir / "registry-images-native-dry-run");
    std::ofstream count_file(config.workdir / "registry-images-native-count");
    count_file << count << '\n';
  }
}

void sync_trivy_databases(const Config& config) {
  std::size_t count = 0;
  for (const auto& image : config.registry_sync.trivy_images) {
    ++count;
    if (dry_run()) {
      continue;
    }
    copy_image(config, config.registry_sync.trivy_db_source + "/" + image,
               registry_image_base(config) + "/" + image);
  }
  if (dry_run()) {
    touch_marker(config.workdir / "registry-trivy-native-dry-run");
    std::ofstream count_file(config.workdir / "registry-trivy-native-count");
    count_file << count << '\n';
  }
}

void run_helm(const Config& config, const std::vector<std::string>& args) {
  std::vector<std::string> command{config.registry_sync.helm_binary};
  command.insert(command.end(), args.begin(), args.end());
  run_command(command);
}

void sync_charts(const Config& config) {
  const auto package = config.workdir / "registry" / "charts";
  std::filesystem::create_directories(package);
  std::size_t count = 0;
  for (const auto& chart : discover_chart_entries(config)) {
    const auto& name = chart.name;
    const auto& repo_url = chart.repo_url;
    const auto& version = chart.version;
    const auto& target_path = chart.target_path;
    if (name.empty() || repo_url.empty()) {
      continue;
    }
    ++count;
    if (dry_run()) {
      continue;
    }
    if (repo_url.rfind("oci://", 0) == 0) {
      std::vector<std::string> pull{"pull", repo_url + "/" + name};
      if (!version.empty()) {
        pull.insert(pull.end(), {"--version", version});
      }
      pull.insert(pull.end(), {"--destination", package.string()});
      run_helm(config, pull);
    } else {
      std::vector<std::string> pull{"pull", name, "--repo", repo_url};
      if (!version.empty()) {
        pull.insert(pull.end(), {"--version", version});
      }
      pull.insert(pull.end(), {"--destination", package.string()});
      run_helm(config, pull);
    }
    std::string package_name;
    for (const auto& entry : std::filesystem::directory_iterator(package)) {
      if (entry.is_regular_file() &&
          entry.path().filename().string().rfind(name + "-", 0) == 0 &&
          entry.path().extension() == ".tgz") {
        package_name = entry.path().filename().string();
      }
    }
    if (package_name.empty()) {
      throw std::runtime_error("unable to find chart package for " + name);
    }
    run_helm(config, {"push",
                      "--plain-http",
                      (package / package_name).string(),
                      "oci://" + registry_base(config) + "/" +
                          config.registry_sync.registry_namespace + "/" +
                          target_path});
  }
  if (dry_run()) {
    touch_marker(config.workdir / "registry-charts-native-dry-run");
    std::ofstream count_file(config.workdir / "registry-charts-native-count");
    count_file << count << '\n';
  }
}

void sync_ol8_oval(const Config& config) {
  if (dry_run()) {
    touch_marker(config.workdir / "registry-oval-native-dry-run");
    std::ofstream count_file(config.workdir / "registry-oval-native-count");
    count_file << "1\n";
    return;
  }

  const auto scan_dir = config.workdir / "registry" / "scan";
  std::filesystem::create_directories(scan_dir);
  const auto oval_file = scan_dir / config.registry_sync.ol8_oval_db_file;
  run_command({config.registry_sync.curl_binary, "--fail", "-Lo", oval_file.string(),
               config.registry_sync.ol8_oval_url});
  const auto container_name = "upstreamd-oval";
  run_command({config.registry_sync.buildah_binary, "from",
               config.registry_sync.buildah_base_image});
  run_command({config.registry_sync.buildah_binary, "config", "--workingdir", "/oval/",
               container_name});
  run_command({config.registry_sync.buildah_binary, "copy", container_name,
               oval_file.string(), "/oval/"});
  run_command({config.registry_sync.buildah_binary, "commit", container_name,
               registry_image_base(config) + "/" + config.registry_sync.ol8_oval_image_ref});
  run_command({config.registry_sync.buildah_binary, "push", "--tls-verify=false",
               registry_image_base(config) + "/" + config.registry_sync.ol8_oval_image_ref});
}

}  // namespace

void run_registry_native(const Config& config) {
  sync_trivy_databases(config);
  sync_ol8_oval(config);
  sync_images(config);
  sync_charts(config);
  promote_once(config);
}

}  // namespace upstreamd
