#include "repository_sync.hpp"

#include "promote.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace upstreamd {
namespace {

std::filesystem::path repository_root(const Config& config) {
  return config.workdir / "repository";
}

std::filesystem::path repository_keys_dir(const Config& config) {
  return repository_root(config) / "keys";
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

bool dry_run() {
  const char* value = std::getenv("UPSTREAMD_SYNC_DRY_RUN");
  if (value == nullptr) {
    return false;
  }
  const std::string text(value);
  return text == "1" || text == "true";
}

void touch_marker(const std::filesystem::path& path) {
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("unable to create marker: " + path.string());
  }
  output << "ok\n";
}

void run_command(const std::vector<std::string>& command) {
  if (command.empty()) {
    throw std::runtime_error("missing repository helper command");
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    throw std::runtime_error("failed to fork repository helper");
  }

  if (pid == 0) {
    auto argv = make_argv(command);
    ::execvp(command.front().c_str(), argv.data());
    _exit(127);
  }

  int status = 0;
  if (::waitpid(pid, &status, 0) < 0) {
    throw std::runtime_error("failed waiting for repository helper process");
  }
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    throw std::runtime_error("repository helper command failed");
  }
}

bool has_repodata(const std::filesystem::path& path) {
  return std::filesystem::is_regular_file(path / "repodata" / "repomd.xml");
}

std::string section_name_from_relative_path(const std::string& relative_path) {
  std::string section = relative_path;
  std::replace(section.begin(), section.end(), '/', '-');
  return section;
}

std::vector<std::string> list_versions(const Config& config) {
  std::vector<std::string> versions;
  for (const auto& entry : std::filesystem::directory_iterator(repository_root(config))) {
    if (!entry.is_directory()) {
      continue;
    }
    const auto name = entry.path().filename().string();
    if (name == "keys" || name == "grype") {
      continue;
    }
    versions.push_back(name);
  }
  std::sort(versions.begin(), versions.end());
  return versions;
}

std::vector<std::string> copy_key_files(const Config& config) {
  std::set<std::string> copied;
  std::error_code error;
  std::filesystem::create_directories(repository_keys_dir(config), error);
  if (error) {
    throw std::runtime_error("failed to create repository keys directory: " +
                             error.message());
  }

  for (const auto& key_path : config.repository_sync.repository_gpg_key_files) {
    if (!std::filesystem::is_regular_file(key_path)) {
      continue;
    }
    const auto destination = repository_keys_dir(config) / key_path.filename();
    std::filesystem::copy_file(key_path, destination,
                               std::filesystem::copy_options::overwrite_existing,
                               error);
    if (error) {
      throw std::runtime_error("failed to copy GPG key " + key_path.string() +
                               ": " + error.message());
    }
    copied.insert(key_path.filename().string());
  }

  return {copied.begin(), copied.end()};
}

std::vector<std::filesystem::path> discover_repo_files(const Config& config) {
  return discover_config_files(config.repository_sync.repo_files_dir, ".repo");
}

std::vector<std::string> parse_repo_ids(const std::filesystem::path& repo_file) {
  std::ifstream input(repo_file);
  if (!input) {
    throw std::runtime_error("unable to open repo file: " + repo_file.string());
  }

  std::vector<std::string> repo_ids;
  std::string line;
  while (std::getline(input, line)) {
    const auto first = line.find_first_not_of(" \t\r\n");
    if (first == std::string::npos || line[first] == '#') {
      continue;
    }
    const auto last = line.find_last_not_of(" \t\r\n");
    const auto trimmed = line.substr(first, last - first + 1);
    if (trimmed.size() >= 3 && trimmed.front() == '[' && trimmed.back() == ']') {
      repo_ids.push_back(trimmed.substr(1, trimmed.size() - 2));
    }
  }
  return repo_ids;
}

std::string version_name_from_repo_file(const std::filesystem::path& repo_file) {
  auto stem = repo_file.stem().string();
  const std::string prefix = "oracle-linux-";
  if (stem.rfind(prefix, 0) == 0) {
    stem = stem.substr(prefix.size());
  }
  if (std::regex_match(stem, std::regex("ol[0-9]+"))) {
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char ch) {
      return static_cast<char>(std::toupper(ch));
    });
  }
  return stem;
}

void sync_repository_mirrors(const Config& config) {
  std::size_t total_repoids = 0;
  for (const auto& repo_file : discover_repo_files(config)) {
    const auto version = version_name_from_repo_file(repo_file);
    const auto destination_root = repository_root(config) / version;
    std::filesystem::create_directories(destination_root);

    for (const auto& repo_id : parse_repo_ids(repo_file)) {
      ++total_repoids;
      if (dry_run()) {
        continue;
      }
      run_command({config.repository_sync.reposync_binary,
                   "--gpgcheck",
                   "--newest-only",
                   "--delete",
                   "--download-metadata",
                   "-c",
                   repo_file.string(),
                   "--exclude=*.src,*.nosrc",
                   "-p",
                   destination_root.string(),
                   "--remote-time",
                   "--repoid",
                   repo_id});
    }
  }

  if (dry_run()) {
    touch_marker(repository_root(config) / "reposync-native-dry-run");
    std::ofstream count_file(repository_root(config) / "reposync-native-count");
    count_file << total_repoids << '\n';
  }
}

std::string format_gpgkey_value(const Config& config,
                                const std::vector<std::string>& key_names) {
  std::ostringstream output;
  bool first = true;
  for (const auto& key_name : key_names) {
    if (!first) {
      output << ' ';
    }
    first = false;
    output << config.repository_sync.repository_baseurl << "/keys/" << key_name;
  }
  return output.str();
}

std::vector<std::string> list_repo_sections(const std::filesystem::path& version_root) {
  std::vector<std::string> sections;
  if (!std::filesystem::is_directory(version_root)) {
    return sections;
  }

  for (const auto& entry : std::filesystem::recursive_directory_iterator(version_root)) {
    if (!entry.is_directory()) {
      continue;
    }
    if (has_repodata(entry.path())) {
      sections.push_back(
          std::filesystem::relative(entry.path(), version_root).generic_string());
    }
  }
  std::sort(sections.begin(), sections.end());
  return sections;
}

void write_repo_file(const Config& config,
                     const std::string& version,
                     const std::vector<std::string>& key_names);

std::vector<std::filesystem::path> list_iso_files(const Config& config) {
  return discover_config_files(config.repository_sync.iso_dir, ".iso");
}

std::filesystem::path make_staging_directory() {
  std::array<char, 64> buffer{};
  std::snprintf(buffer.data(), buffer.size(), "/tmp/upstreamd-nisp-XXXXXX");
  char* created = ::mkdtemp(buffer.data());
  if (created == nullptr) {
    throw std::runtime_error("failed to create NISP staging directory");
  }
  return created;
}

void extract_iso(const Config& config,
                 const std::filesystem::path& iso_path,
                 const std::filesystem::path& staging_dir) {
  std::vector<std::string> command;
  command.push_back(config.repository_sync.repository_iso_extractor);
  command.insert(command.end(),
                 config.repository_sync.repository_iso_extractor_args.begin(),
                 config.repository_sync.repository_iso_extractor_args.end());
  command.push_back(iso_path.string());
  command.push_back("-C");
  command.push_back(staging_dir.string());
  run_command(command);
}

std::string parse_nisp_version(const std::filesystem::path& version_file) {
  std::ifstream input(version_file);
  if (!input) {
    throw std::runtime_error("unable to read NISP version file: " +
                             version_file.string());
  }

  std::string line;
  while (std::getline(input, line)) {
    const auto colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }

    auto key = line.substr(0, colon);
    auto value = line.substr(colon + 1);

    auto trim = [](std::string text) {
      const auto first = text.find_first_not_of(" \t\r\n");
      if (first == std::string::npos) {
        return std::string{};
      }
      const auto last = text.find_last_not_of(" \t\r\n");
      return text.substr(first, last - first + 1);
    };

    key = trim(key);
    value = trim(value);
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });

    if ((value.size() >= 2 && value.front() == '"' && value.back() == '"') ||
        (value.size() >= 2 && value.front() == '\'' && value.back() == '\'')) {
      value = value.substr(1, value.size() - 2);
    }

    if (key == "media" && !value.empty()) {
      return value;
    }
  }

  throw std::runtime_error("unable to derive NISP version from " +
                           version_file.string());
}

std::vector<std::string> copy_nisp_keys(const Config& config,
                                        const std::filesystem::path& staging_dir) {
  std::set<std::string> copied;
  std::error_code error;
  std::filesystem::create_directories(repository_keys_dir(config), error);
  if (error) {
    throw std::runtime_error("failed to create repository keys directory: " +
                             error.message());
  }

  for (const auto& entry : std::filesystem::directory_iterator(staging_dir)) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const auto name = entry.path().filename().string();
    if (name.find("-GPG-KEY") == std::string::npos) {
      continue;
    }
    const auto destination = repository_keys_dir(config) / entry.path().filename();
    std::filesystem::copy_file(entry.path(), destination,
                               std::filesystem::copy_options::overwrite_existing,
                               error);
    if (error) {
      throw std::runtime_error("failed to copy NISP key " + entry.path().string() +
                               ": " + error.message());
    }
    copied.insert(name);
  }

  return {copied.begin(), copied.end()};
}

void move_directory_contents(const std::filesystem::path& source_dir,
                             const std::filesystem::path& target_dir) {
  std::error_code error;
  std::filesystem::create_directories(target_dir, error);
  if (error) {
    throw std::runtime_error("failed to create target directory " +
                             target_dir.string() + ": " + error.message());
  }

  for (const auto& entry : std::filesystem::directory_iterator(source_dir)) {
    const auto destination = target_dir / entry.path().filename();
    error.clear();
    std::filesystem::rename(entry.path(), destination, error);
    if (!error) {
      continue;
    }
    if (error.value() != EXDEV) {
      throw std::runtime_error("failed to move extracted NISP content from " +
                               entry.path().string() + ": " + error.message());
    }
    error.clear();
    std::filesystem::copy(entry.path(), destination,
                          std::filesystem::copy_options::recursive |
                              std::filesystem::copy_options::overwrite_existing,
                          error);
    if (error) {
      throw std::runtime_error("failed to copy extracted NISP content from " +
                               entry.path().string() + ": " + error.message());
    }
    std::filesystem::remove_all(entry.path(), error);
    if (error) {
      throw std::runtime_error("failed to clean extracted NISP staging path " +
                               entry.path().string() + ": " + error.message());
    }
  }
}

void import_nisp_isos(const Config& config, std::vector<std::string>& key_names) {
  for (const auto& iso_path : list_iso_files(config)) {
    const auto staging_dir = make_staging_directory();
    try {
      extract_iso(config, iso_path, staging_dir);
      const auto version_file = staging_dir / "nisp.version";
      if (!std::filesystem::is_regular_file(version_file)) {
        throw std::runtime_error("NISP ISO missing nisp.version: " +
                                 iso_path.string());
      }

      const auto version = parse_nisp_version(version_file);
      const auto repo_file = repository_root(config) / (version + ".repo");
      const auto target_dir = repository_root(config) / version;

      if (std::filesystem::exists(repo_file)) {
        std::filesystem::remove_all(staging_dir);
        continue;
      }
      if (std::filesystem::exists(target_dir)) {
        throw std::runtime_error("target directory already exists for NISP version " +
                                 version + ": " + target_dir.string());
      }

      for (const auto& key_name : copy_nisp_keys(config, staging_dir)) {
        key_names.push_back(key_name);
      }
      std::sort(key_names.begin(), key_names.end());
      key_names.erase(std::unique(key_names.begin(), key_names.end()),
                      key_names.end());

      move_directory_contents(staging_dir, target_dir);
      std::filesystem::remove_all(staging_dir);
      write_repo_file(config, version, key_names);
    } catch (...) {
      std::filesystem::remove_all(staging_dir);
      throw;
    }
  }
}

void update_grype_database(const Config& config) {
  if (!config.repository_sync.grype_enabled) {
    return;
  }

  if (dry_run()) {
    touch_marker(repository_root(config) / "grype-native-dry-run");
    return;
  }

  const auto target_dir = repository_root(config) / config.repository_sync.grype_db_subdir;
  std::filesystem::create_directories(target_dir);
  const auto staging_dir = make_staging_directory();
  try {
    const auto latest_json = staging_dir / "latest.json";
    run_command({config.repository_sync.curl_binary, "-Lo", latest_json.string(),
                 config.repository_sync.grype_db_url + "/latest.json"});

    std::ifstream input(latest_json);
    if (!input) {
      throw std::runtime_error("unable to read Grype latest.json");
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    const auto latest_text = buffer.str();

    std::smatch path_match;
    std::smatch checksum_match;
    if (!std::regex_search(latest_text, path_match,
                           std::regex("\"path\"\\s*:\\s*\"([^\"]+)\"")) ||
        !std::regex_search(latest_text, checksum_match,
                           std::regex("\"checksum\"\\s*:\\s*\"sha256:([^\"]+)\""))) {
      throw std::runtime_error("unable to parse Grype latest.json");
    }

    const auto db_filename = path_match[1].str();
    const auto expected_checksum = checksum_match[1].str();
    const auto archive_path = staging_dir / db_filename;
    run_command({config.repository_sync.curl_binary, "-Lo", archive_path.string(),
                 config.repository_sync.grype_db_url + "/" + db_filename});

    auto checksum_output =
        capture_output({config.repository_sync.sha256sum_binary, archive_path.string()});
    const auto first_space = checksum_output.find(' ');
    const auto actual_checksum = checksum_output.substr(0, first_space);
    if (actual_checksum != expected_checksum) {
      throw std::runtime_error("Grype checksum verification failed");
    }

    std::filesystem::copy_file(latest_json, target_dir / "latest.json",
                               std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(archive_path, target_dir / db_filename,
                               std::filesystem::copy_options::overwrite_existing);
  } catch (...) {
    std::filesystem::remove_all(staging_dir);
    throw;
  }
  std::filesystem::remove_all(staging_dir);
}

void write_repo_file(const Config& config,
                     const std::string& version,
                     const std::vector<std::string>& key_names) {
  const auto source_dir = repository_root(config) / version;
  const auto repo_file = repository_root(config) / (version + ".repo");
  const auto base_url = config.repository_sync.repository_baseurl + "/" + version;
  const auto gpgkey_value = format_gpgkey_value(config, key_names);

  std::ofstream output(repo_file);
  if (!output) {
    throw std::runtime_error("unable to write repo file: " + repo_file.string());
  }

  for (const auto& section : list_repo_sections(source_dir)) {
    output << '[' << section_name_from_relative_path(section) << "]\n";
    output << "baseurl=" << base_url << '/' << section << "\n";
    output << "name=" << version << '-' << section_name_from_relative_path(section)
           << "\n";
    output << "enabled=0\n";
    output << "gpgcheck=1\n";
    if (!gpgkey_value.empty()) {
      output << "gpgkey=" << gpgkey_value << "\n";
    }
    output << "skip_if_unavailable=True\n\n";
  }
}

void write_index_file(const std::filesystem::path& path,
                      const std::vector<std::string>& values) {
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("unable to write index file: " + path.string());
  }
  for (const auto& value : values) {
    output << value << '\n';
  }
}

void refresh_indexes(const Config& config) {
  std::vector<std::string> repo_files;
  std::vector<std::string> key_files;

  for (const auto& entry : std::filesystem::directory_iterator(repository_root(config))) {
    if (entry.is_regular_file() && entry.path().extension() == ".repo") {
      repo_files.push_back(entry.path().filename().string());
    }
  }
  std::sort(repo_files.begin(), repo_files.end());

  if (std::filesystem::is_directory(repository_keys_dir(config))) {
    for (const auto& entry : std::filesystem::directory_iterator(repository_keys_dir(config))) {
      if (!entry.is_regular_file()) {
        continue;
      }
      const auto name = entry.path().filename().string();
      if (name == "list") {
        continue;
      }
      key_files.push_back(name);
    }
  }
  std::sort(key_files.begin(), key_files.end());

  write_index_file(repository_root(config) / "list", repo_files);
  write_index_file(repository_keys_dir(config) / "list", key_files);
}

}  // namespace

void run_repository_native(const Config& config) {
  std::error_code error;
  std::filesystem::create_directories(repository_root(config), error);
  if (error) {
    throw std::runtime_error("failed to prepare repository root: " + error.message());
  }

  sync_repository_mirrors(config);
  auto key_names = copy_key_files(config);
  update_grype_database(config);
  import_nisp_isos(config, key_names);
  for (const auto& version : list_versions(config)) {
    write_repo_file(config, version, key_names);
  }
  refresh_indexes(config);
  promote_once(config);
}

}  // namespace upstreamd
