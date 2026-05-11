#include "validate.hpp"

#include <cerrno>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>

namespace upstreamd {
namespace {

void require_directory(const std::filesystem::path& path) {
  if (!std::filesystem::is_directory(path)) {
    throw std::runtime_error("missing required directory: " + path.string());
  }
}

void require_file(const std::filesystem::path& path) {
  if (!std::filesystem::is_regular_file(path)) {
    throw std::runtime_error("missing required file: " + path.string());
  }
}

void require_writable_directory(const std::filesystem::path& path) {
  const auto probe = path / (".upstreamd-dir-probe-" + std::to_string(::getpid()));
  std::error_code error;
  std::filesystem::create_directory(probe, error);
  if (error) {
    throw std::runtime_error("directory is not writable: " + path.string() +
                             " (" + error.message() + ")");
  }
  std::filesystem::remove(probe, error);
}

void require_same_filesystem(const std::filesystem::path& left,
                             const std::filesystem::path& right) {
  const auto left_space = std::filesystem::space(left);
  const auto right_space = std::filesystem::space(right);
  if (left_space.capacity != right_space.capacity ||
      left_space.available != right_space.available) {
    const auto left_probe = left / (".upstreamd-fs-left-" + std::to_string(::getpid()));
    const auto right_probe =
        right / (".upstreamd-fs-right-" + std::to_string(::getpid()));

    {
      std::ofstream output(left_probe);
      if (!output) {
        throw std::runtime_error("unable to create filesystem probe in " +
                                 left.string());
      }
      output << "probe\n";
    }

    if (::link(left_probe.c_str(), right_probe.c_str()) != 0) {
      const auto message = std::string(std::strerror(errno));
      std::filesystem::remove(left_probe);
      throw std::runtime_error("hard-link precheck failed between " +
                               left.string() + " and " + right.string() + ": " +
                               message);
    }

    std::filesystem::remove(right_probe);
    std::filesystem::remove(left_probe);
  }
}

void require_hardlink(const std::filesystem::path& source_dir,
                      const std::filesystem::path& target_dir) {
  const auto source_file =
      source_dir / (".upstreamd-link-source-" + std::to_string(::getpid()));
  const auto target_file =
      target_dir / (".upstreamd-link-target-" + std::to_string(::getpid()));

  {
    std::ofstream output(source_file);
    if (!output) {
      throw std::runtime_error("unable to create probe file in " +
                               source_dir.string());
    }
    output << "probe\n";
  }

  if (::link(source_file.c_str(), target_file.c_str()) != 0) {
    const auto message = std::string(std::strerror(errno));
    std::filesystem::remove(source_file);
    throw std::runtime_error("unable to hard-link " + source_dir.string() +
                             " to " + target_dir.string() + ": " + message);
  }

  std::filesystem::remove(target_file);
  std::filesystem::remove(source_file);
}

void require_cron_like(const std::string& schedule, const std::string& label) {
  if (schedule.empty()) {
    throw std::runtime_error("missing schedule for " + label);
  }

  std::size_t fields = 0;
  bool in_field = false;
  for (char ch : schedule) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      if (in_field) {
        ++fields;
        in_field = false;
      }
    } else {
      in_field = true;
    }
  }
  if (in_field) {
    ++fields;
  }

  if (fields != 5) {
    throw std::runtime_error("invalid cron-style schedule for " + label + ": " +
                             schedule);
  }
}

void require_command(const std::vector<std::string>& command,
                     const std::string& label) {
  if (command.empty()) {
    return;
  }
  if (command.front().empty()) {
    throw std::runtime_error("invalid empty command for " + label);
  }
}

}  // namespace

void validate_startup(const Config& config) {
  require_directory(config.workdir);
  require_writable_directory(config.workdir);
  require_directory(config.transfer_root);
  require_writable_directory(config.transfer_root);

  for (const auto& area : config.watched_areas) {
    const auto source_dir = config.workdir / area;
    const auto transfer_dir = config.transfer_root / area;

    require_directory(source_dir);
    require_writable_directory(source_dir);
    require_directory(transfer_dir);
    require_writable_directory(transfer_dir);
    require_same_filesystem(source_dir, transfer_dir);
    require_hardlink(source_dir, transfer_dir);
  }

  if (config.repository_sync.enabled) {
    require_cron_like(config.repository_sync.schedule, "repositories");
    require_command(config.repository_sync.command, "repositories");
    require_directory(config.repository_sync.repo_files_dir);
    require_directory(config.repository_sync.iso_dir);
  }

  if (config.registry_sync.enabled) {
    require_cron_like(config.registry_sync.schedule, "registries");
    require_command(config.registry_sync.command, "registries");
    require_file(config.registry_sync.images_yaml);
    require_file(config.registry_sync.charts_yaml);
  }
}

}  // namespace upstreamd
