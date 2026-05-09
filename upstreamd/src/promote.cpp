#include "promote.hpp"

#include <sys/inotify.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace upstreamd {
namespace {

constexpr uint32_t kWatchMask = IN_CLOSE_WRITE | IN_CREATE | IN_MOVED_TO | IN_ATTRIB;

void ensure_parent_directory(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    throw std::runtime_error("failed to create parent directory for " +
                             path.string() + ": " + error.message());
  }
}

bool same_file(const std::filesystem::path& left, const std::filesystem::path& right) {
  std::error_code error;
  const bool result = std::filesystem::equivalent(left, right, error);
  if (error) {
    return false;
  }
  return result;
}

void link_or_replace(const std::filesystem::path& source,
                     const std::filesystem::path& target) {
  ensure_parent_directory(target);

  if (std::filesystem::exists(target) && same_file(source, target)) {
    return;
  }

  std::error_code error;
  std::filesystem::remove(target, error);

  if (::link(source.c_str(), target.c_str()) != 0) {
    throw std::runtime_error("failed to hard-link " + source.string() + " -> " +
                             target.string() + ": " + std::strerror(errno));
  }
}

void sync_area(const std::filesystem::path& source_root,
               const std::filesystem::path& transfer_root) {
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(source_root)) {
    const auto relative = std::filesystem::relative(entry.path(), source_root);
    const auto target = transfer_root / relative;

    if (entry.is_directory()) {
      std::error_code error;
      std::filesystem::create_directories(target, error);
      if (error) {
        throw std::runtime_error("failed to create transfer directory " +
                                 target.string() + ": " + error.message());
      }
      continue;
    }

    if (entry.is_regular_file()) {
      link_or_replace(entry.path(), target);
    }
  }
}

void add_watch_tree(int inotify_fd,
                    const std::filesystem::path& root,
                    std::map<int, std::filesystem::path>& watch_map) {
  auto add_single_watch = [&](const std::filesystem::path& path) {
    const int watch_fd = ::inotify_add_watch(inotify_fd, path.c_str(), kWatchMask);
    if (watch_fd < 0) {
      throw std::runtime_error("failed to add inotify watch for " + path.string() +
                               ": " + std::strerror(errno));
    }
    watch_map[watch_fd] = path;
  };

  add_single_watch(root);
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_directory()) {
      add_single_watch(entry.path());
    }
  }
}

void handle_file_event(const Config& config,
                       const std::filesystem::path& full_path,
                       std::map<int, std::filesystem::path>& watch_map,
                       int inotify_fd) {
  for (const auto& area : config.watched_areas) {
    const auto source_root = config.workdir / area;
    if (full_path == source_root ||
        full_path.string().rfind(source_root.string() + "/", 0) == 0) {
      const auto transfer_root = config.transfer_root / area;
      if (std::filesystem::is_directory(full_path)) {
        const auto watch_fd =
            ::inotify_add_watch(inotify_fd, full_path.c_str(), kWatchMask);
        if (watch_fd >= 0) {
          watch_map[watch_fd] = full_path;
        }

        const auto relative = std::filesystem::relative(full_path, source_root);
        std::error_code error;
        std::filesystem::create_directories(transfer_root / relative, error);
        if (error) {
          throw std::runtime_error("failed to create transfer directory " +
                                   (transfer_root / relative).string() + ": " +
                                   error.message());
        }
        return;
      }

      if (std::filesystem::is_regular_file(full_path)) {
        const auto relative = std::filesystem::relative(full_path, source_root);
        link_or_replace(full_path, transfer_root / relative);
      }
      return;
    }
  }
}

}  // namespace

void promote_once(const Config& config) {
  for (const auto& area : config.watched_areas) {
    sync_area(config.workdir / area, config.transfer_root / area);
  }
}

void watch_and_promote(const Config& config, int watch_seconds) {
  promote_once(config);

  const int inotify_fd = ::inotify_init1(IN_NONBLOCK);
  if (inotify_fd < 0) {
    throw std::runtime_error(std::string("failed to initialize inotify: ") +
                             std::strerror(errno));
  }

  std::map<int, std::filesystem::path> watch_map;
  for (const auto& area : config.watched_areas) {
    add_watch_tree(inotify_fd, config.workdir / area, watch_map);
  }

  auto cleanup = [&]() { ::close(inotify_fd); };

  std::vector<char> buffer(64 * 1024);
  const auto deadline = watch_seconds > 0
                            ? std::chrono::steady_clock::now() +
                                  std::chrono::seconds(watch_seconds)
                            : std::chrono::steady_clock::time_point::max();
  auto next_rescan = std::chrono::steady_clock::now() + std::chrono::seconds(1);

  while (std::chrono::steady_clock::now() < deadline) {
    const ssize_t bytes_read = ::read(inotify_fd, buffer.data(), buffer.size());
    if (bytes_read < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        if (std::chrono::steady_clock::now() >= next_rescan) {
          promote_once(config);
          next_rescan = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        continue;
      }
      cleanup();
      throw std::runtime_error(std::string("failed to read inotify events: ") +
                               std::strerror(errno));
    }

    std::size_t offset = 0;
    while (offset < static_cast<std::size_t>(bytes_read)) {
      const auto* event =
          reinterpret_cast<const struct inotify_event*>(buffer.data() + offset);
      auto watched = watch_map.find(event->wd);
      if (watched != watch_map.end()) {
        auto full_path = watched->second;
        if (event->len > 0 && event->name[0] != '\0') {
          full_path /= event->name;
        }
        handle_file_event(config, full_path, watch_map, inotify_fd);
      }
      offset += sizeof(struct inotify_event) + event->len;
    }
  }

  cleanup();
}

}  // namespace upstreamd
