#include "promote.hpp"

#include <sys/inotify.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
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

constexpr uint32_t kDefaultWatchMask =
    IN_CLOSE_WRITE | IN_CREATE | IN_MOVED_TO | IN_ATTRIB;

bool watch_debug_enabled() {
  const char* value = std::getenv("UPSTREAMD_WATCH_DEBUG");
  if (value == nullptr) {
    return false;
  }
  return std::string(value) == "1" || std::string(value) == "true";
}

uint32_t watch_mask() {
  return watch_debug_enabled() ? IN_ALL_EVENTS : kDefaultWatchMask;
}

std::string describe_mask(uint32_t mask) {
  struct MaskName {
    uint32_t value;
    const char* name;
  };

  static const MaskName names[] = {
      {IN_ACCESS, "IN_ACCESS"},
      {IN_ATTRIB, "IN_ATTRIB"},
      {IN_CLOSE_WRITE, "IN_CLOSE_WRITE"},
      {IN_CLOSE_NOWRITE, "IN_CLOSE_NOWRITE"},
      {IN_CREATE, "IN_CREATE"},
      {IN_DELETE, "IN_DELETE"},
      {IN_DELETE_SELF, "IN_DELETE_SELF"},
      {IN_MODIFY, "IN_MODIFY"},
      {IN_MOVE_SELF, "IN_MOVE_SELF"},
      {IN_MOVED_FROM, "IN_MOVED_FROM"},
      {IN_MOVED_TO, "IN_MOVED_TO"},
      {IN_OPEN, "IN_OPEN"},
      {IN_IGNORED, "IN_IGNORED"},
      {IN_ISDIR, "IN_ISDIR"},
      {IN_Q_OVERFLOW, "IN_Q_OVERFLOW"},
      {IN_UNMOUNT, "IN_UNMOUNT"},
  };

  std::string result;
  for (const auto& entry : names) {
    if ((mask & entry.value) == 0) {
      continue;
    }
    if (!result.empty()) {
      result += "|";
    }
    result += entry.name;
  }
  if (result.empty()) {
    result = "0";
  }
  return result;
}

void debug_log(const std::string& message) {
  if (watch_debug_enabled()) {
    std::cerr << "watch-debug: " << message << '\n';
  }
}

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
    const int watch_fd = ::inotify_add_watch(inotify_fd, path.c_str(), watch_mask());
    if (watch_fd < 0) {
      throw std::runtime_error("failed to add inotify watch for " + path.string() +
                               ": " + std::strerror(errno));
    }
    watch_map[watch_fd] = path;
    debug_log("added watch wd=" + std::to_string(watch_fd) + " path=" + path.string() +
              " mask=" + describe_mask(watch_mask()));
  };

  add_single_watch(root);
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (entry.is_directory()) {
      add_single_watch(entry.path());
    }
  }
}

bool path_is_watched(const std::map<int, std::filesystem::path>& watch_map,
                     const std::filesystem::path& path) {
  for (const auto& entry : watch_map) {
    if (entry.second == path) {
      return true;
    }
  }
  return false;
}

bool is_directory_create_event(uint32_t mask) {
  return (mask & IN_ISDIR) != 0 && ((mask & IN_CREATE) != 0 || (mask & IN_MOVED_TO) != 0);
}

bool is_file_sync_event(uint32_t mask) {
  if ((mask & IN_ISDIR) != 0) {
    return false;
  }
  return (mask & (IN_CLOSE_WRITE | IN_CREATE | IN_MOVED_TO | IN_ATTRIB | IN_MODIFY)) != 0;
}

void handle_file_event(const Config& config,
                       const std::filesystem::path& full_path,
                       uint32_t mask,
                       std::map<int, std::filesystem::path>& watch_map,
                       int inotify_fd) {
  for (const auto& area : config.watched_areas) {
    const auto source_root = config.workdir / area;
    if (full_path == source_root ||
        full_path.string().rfind(source_root.string() + "/", 0) == 0) {
      const auto transfer_root = config.transfer_root / area;
      if (is_directory_create_event(mask) && std::filesystem::is_directory(full_path)) {
        if (path_is_watched(watch_map, full_path)) {
          return;
        }
        const auto watch_fd =
            ::inotify_add_watch(inotify_fd, full_path.c_str(), watch_mask());
        if (watch_fd >= 0) {
          watch_map[watch_fd] = full_path;
          debug_log("added nested watch wd=" + std::to_string(watch_fd) +
                    " path=" + full_path.string() +
                    " mask=" + describe_mask(watch_mask()));
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

      if (is_file_sync_event(mask) && std::filesystem::is_regular_file(full_path)) {
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

  while (std::chrono::steady_clock::now() < deadline) {
    const ssize_t bytes_read = ::read(inotify_fd, buffer.data(), buffer.size());
    if (bytes_read < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
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
        debug_log("event wd=" + std::to_string(event->wd) +
                  " mask=" + describe_mask(event->mask) +
                  " path=" + full_path.string());
        handle_file_event(config, full_path, event->mask, watch_map, inotify_fd);
      }
      offset += sizeof(struct inotify_event) + event->len;
    }
  }

  cleanup();
}

}  // namespace upstreamd
