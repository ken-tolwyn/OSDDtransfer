#include "scheduler.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace upstreamd {
namespace {

using Clock = std::chrono::system_clock;

std::set<int> parse_field(const std::string& text, int min_value, int max_value) {
  std::set<int> values;
  if (text == "*") {
    for (int value = min_value; value <= max_value; ++value) {
      values.insert(value);
    }
    return values;
  }

  std::stringstream stream(text);
  std::string token;
  while (std::getline(stream, token, ',')) {
    if (token.empty()) {
      throw std::runtime_error("invalid cron field: " + text);
    }

    const auto slash = token.find('/');
    std::string base = token;
    int step = 1;
    if (slash != std::string::npos) {
      base = token.substr(0, slash);
      step = std::stoi(token.substr(slash + 1));
      if (step <= 0) {
        throw std::runtime_error("invalid cron step in field: " + text);
      }
    }

    int start = min_value;
    int end = max_value;
    if (base.empty() || base == "*") {
      start = min_value;
      end = max_value;
    } else {
      const auto dash = base.find('-');
      if (dash == std::string::npos) {
        start = end = std::stoi(base);
      } else {
        start = std::stoi(base.substr(0, dash));
        end = std::stoi(base.substr(dash + 1));
      }
    }

    if (start < min_value || end > max_value || start > end) {
      throw std::runtime_error("cron field out of range: " + text);
    }

    for (int value = start; value <= end; value += step) {
      values.insert(value);
    }
  }

  return values;
}

struct CronExpression {
  std::set<int> minutes;
  std::set<int> hours;
  std::set<int> month_days;
  std::set<int> months;
  std::set<int> week_days;
};

CronExpression parse_cron_expression(const std::string& text) {
  std::stringstream stream(text);
  std::vector<std::string> fields;
  std::string field;
  while (stream >> field) {
    fields.push_back(field);
  }

  if (fields.size() != 5) {
    throw std::runtime_error("invalid cron expression: " + text);
  }

  return {
      parse_field(fields[0], 0, 59),
      parse_field(fields[1], 0, 23),
      parse_field(fields[2], 1, 31),
      parse_field(fields[3], 1, 12),
      parse_field(fields[4], 0, 6),
  };
}

bool matches(const CronExpression& cron, const std::tm& now) {
  return cron.minutes.contains(now.tm_min) &&
         cron.hours.contains(now.tm_hour) &&
         cron.month_days.contains(now.tm_mday) &&
         cron.months.contains(now.tm_mon + 1) &&
         cron.week_days.contains(now.tm_wday);
}

std::string minute_key(const std::tm& now) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d",
                now.tm_year + 1900, now.tm_mon + 1, now.tm_mday, now.tm_hour,
                now.tm_min);
  return buffer;
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

std::filesystem::path executable_dir() {
  std::vector<char> buffer(4096);
  const ssize_t size = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (size < 0) {
    throw std::runtime_error(std::string("failed to resolve executable path: ") +
                             std::strerror(errno));
  }
  buffer[static_cast<std::size_t>(size)] = '\0';
  return std::filesystem::path(buffer.data()).parent_path();
}

std::vector<std::string> candidate_sync_command(const std::string& target_name) {
  const auto bin_dir = executable_dir();
  std::vector<std::filesystem::path> candidates;

  if (target_name == "repositories") {
    candidates = {
        bin_dir.parent_path() / "libexec" / "sync-repositories.sh",
        bin_dir.parent_path().parent_path() / "repositories" / "sync-repositories.sh",
        std::filesystem::current_path() / "repositories" / "sync-repositories.sh",
    };
  } else if (target_name == "registries") {
    candidates = {
        bin_dir.parent_path() / "libexec" / "sync-registries.sh",
        bin_dir.parent_path().parent_path() / "registries" / "sync-registries.sh",
        std::filesystem::current_path() / "registries" / "sync-registries.sh",
    };
  } else {
    throw std::runtime_error("unsupported sync target: " + target_name);
  }

  for (const auto& candidate : candidates) {
    if (std::filesystem::is_regular_file(candidate)) {
      return {"/usr/bin/bash", candidate.string()};
    }
  }

  throw std::runtime_error("unable to resolve sync script for " + target_name);
}

const SyncPolicy& policy_for_target(const Config& config, const std::string& target_name) {
  if (target_name == "repositories") {
    return config.repository_sync;
  }
  if (target_name == "registries") {
    return config.registry_sync;
  }
  throw std::runtime_error("unknown sync target: " + target_name);
}

std::vector<std::string> resolve_command(const Config& config,
                                         const std::string& target_name) {
  const auto& policy = policy_for_target(config, target_name);
  if (!policy.command.empty()) {
    return policy.command;
  }
  return candidate_sync_command(target_name);
}

void run_command(const Config& config,
                 const std::string& name,
                 const std::vector<std::string>& command) {
  if (command.empty()) {
    throw std::runtime_error("missing command for sync policy: " + name);
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    throw std::runtime_error("failed to fork sync command for " + name + ": " +
                             std::strerror(errno));
  }

  if (pid == 0) {
    ::setenv("UPSTREAMD_WORKDIR", config.workdir.c_str(), 1);
    ::setenv("UPSTREAMD_TRANSFER_ROOT", config.transfer_root.c_str(), 1);
    ::setenv("UPSTREAMD_CONFIG_ROOT", config.config_root.c_str(), 1);
    ::setenv("UPSTREAMD_SCHEDULED_SYNC", name.c_str(), 1);
    auto argv = make_argv(command);
    ::execvp(command.front().c_str(), argv.data());
    _exit(127);
  }

  int status = 0;
  if (::waitpid(pid, &status, 0) < 0) {
    throw std::runtime_error("failed waiting for sync command " + name + ": " +
                             std::strerror(errno));
  }

  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    throw std::runtime_error("sync command failed for " + name);
  }
}

struct ScheduledPolicy {
  std::string name;
  const SyncPolicy* policy;
  CronExpression cron;
  std::string last_run_minute;
};

std::vector<ScheduledPolicy> build_policies(const Config& config) {
  std::vector<ScheduledPolicy> policies;
  if (config.repository_sync.enabled) {
    policies.push_back(
        {"repositories", &config.repository_sync,
         parse_cron_expression(config.repository_sync.schedule), ""});
  }
  if (config.registry_sync.enabled) {
    policies.push_back({"registries", &config.registry_sync,
                        parse_cron_expression(config.registry_sync.schedule), ""});
  }
  return policies;
}

}  // namespace

void run_sync_once(const Config& config, const std::string& target_name) {
  const auto& policy = policy_for_target(config, target_name);
  if (!policy.enabled) {
    throw std::runtime_error("sync target is disabled: " + target_name);
  }
  run_command(config, target_name, resolve_command(config, target_name));
}

void run_scheduler(const Config& config, int run_seconds) {
  auto policies = build_policies(config);
  const auto deadline = run_seconds > 0
                            ? Clock::now() + std::chrono::seconds(run_seconds)
                            : Clock::time_point::max();

  while (Clock::now() < deadline) {
    const auto now_time = Clock::to_time_t(Clock::now());
    std::tm now{};
    localtime_r(&now_time, &now);
    const auto current_minute = minute_key(now);

    for (auto& entry : policies) {
      if (entry.last_run_minute == current_minute) {
        continue;
      }
      if (matches(entry.cron, now)) {
        run_command(config, entry.name, resolve_command(config, entry.name));
        entry.last_run_minute = current_minute;
      }
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

}  // namespace upstreamd
