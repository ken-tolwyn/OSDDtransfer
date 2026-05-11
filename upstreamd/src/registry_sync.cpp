#include "registry_sync.hpp"

#include "promote.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

std::string capture_output(const std::vector<std::string>& command) {
  if (command.empty()) {
    throw std::runtime_error("missing capture command");
  }

  std::string shell_command;
  for (std::size_t i = 0; i < command.size(); ++i) {
    if (i > 0) {
      shell_command += ' ';
    }
    shell_command += '\'';
    for (char ch : command[i]) {
      if (ch == '\'') {
        shell_command += "'\\''";
      } else {
        shell_command += ch;
      }
    }
    shell_command += '\'';
  }

  FILE* pipe = ::popen(shell_command.c_str(), "r");
  if (pipe == nullptr) {
    throw std::runtime_error("failed to start capture command");
  }

  std::string output;
  char buffer[4096];
  while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
  }

  const int status = ::pclose(pipe);
  if (status != 0) {
    throw std::runtime_error("capture command failed");
  }
  return output;
}

std::vector<std::string> split_lines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty()) {
      lines.push_back(line);
    }
  }
  return lines;
}

std::string registry_base(const Config& config) {
  return config.registry_sync.registry_host + ":" +
         std::to_string(config.registry_sync.registry_port);
}

std::string registry_image_base(const Config& config) {
  return registry_base(config) + "/" + config.registry_sync.registry_namespace;
}

std::vector<std::string> discover_image_entries(const Config& config) {
  return split_lines(capture_output(
      {config.registry_sync.yq_binary, "-r",
       "to_entries[] | .key as $registry | .value.images | to_entries[] | .key as "
       "$image | .value[] | [$registry, $image, .] | @tsv",
       config.registry_sync.images_yaml.string()}));
}

std::vector<std::string> discover_chart_entries(const Config& config) {
  return split_lines(capture_output({config.registry_sync.yq_binary, "-o=json", "-I=0",
                                     ".charts[]", config.registry_sync.charts_yaml.string()}));
}

std::string run_yq_stdin(const Config& config,
                         const std::string& json,
                         const std::string& expression) {
  const auto temp = std::filesystem::temp_directory_path() /
                    ("upstreamd-chart-" + std::to_string(::getpid()) + ".json");
  {
    std::ofstream output(temp);
    output << json;
  }
  auto result = capture_output(
      {config.registry_sync.yq_binary, "-r", expression, temp.string()});
  std::filesystem::remove(temp);
  while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
    result.pop_back();
  }
  return result;
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

void run_helm(const Config& config, const std::vector<std::string>& args) {
  std::vector<std::string> command{config.registry_sync.helm_runner,
                                   "run",
                                   "--rm",
                                   "-v",
                                   (config.workdir / "registry" / "charts").string() +
                                       ":/charts:Z",
                                   "-w",
                                   "/charts",
                                   config.registry_sync.helm_container_image};
  command.insert(command.end(), args.begin(), args.end());
  run_command(command);
}

void sync_charts(const Config& config) {
  std::filesystem::create_directories(config.workdir / "registry" / "charts");
  std::size_t count = 0;
  for (const auto& chart_json : discover_chart_entries(config)) {
    const auto name = run_yq_stdin(config, chart_json, ".name");
    const auto repo_url = run_yq_stdin(config, chart_json, ".repoURL");
    const auto version = run_yq_stdin(config, chart_json, ".version // \"\"");
    const auto target_path = run_yq_stdin(config, chart_json, ".targetPath // \"charts\"");
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
      pull.insert(pull.end(), {"--destination", "/charts"});
      run_helm(config, pull);
    } else {
      std::vector<std::string> pull{"pull", name, "--repo", repo_url};
      if (!version.empty()) {
        pull.insert(pull.end(), {"--version", version});
      }
      pull.insert(pull.end(), {"--destination", "/charts"});
      run_helm(config, pull);
    }
    const auto package = config.workdir / "registry" / "charts";
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
                      "/charts/" + package_name,
                      "oci://" + registry_base(config) + "/" +
                          config.registry_sync.registry_chart_namespace + "/" +
                          target_path});
  }
  if (dry_run()) {
    touch_marker(config.workdir / "registry-charts-native-dry-run");
    std::ofstream count_file(config.workdir / "registry-charts-native-count");
    count_file << count << '\n';
  }
}

}  // namespace

void run_registry_native(const Config& config) {
  sync_images(config);
  sync_charts(config);
  promote_once(config);
}

}  // namespace upstreamd
