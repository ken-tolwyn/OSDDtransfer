#include "supervisor.hpp"

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace upstreamd {
namespace {

struct RunningProcess {
  std::string name;
  pid_t pid;
};

std::vector<char*> make_argv(const std::string& binary,
                             const std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size() + 2);
  argv.push_back(const_cast<char*>(binary.c_str()));
  for (const auto& arg : args) {
    argv.push_back(const_cast<char*>(arg.c_str()));
  }
  argv.push_back(nullptr);
  return argv;
}

pid_t start_process(const std::string& name,
                    const std::string& binary,
                    const std::vector<std::string>& args) {
  const pid_t pid = ::fork();
  if (pid < 0) {
    throw std::runtime_error("failed to fork " + name + ": " +
                             std::strerror(errno));
  }

  if (pid == 0) {
    auto argv = make_argv(binary, args);
    ::execvp(binary.c_str(), argv.data());
    _exit(127);
  }

  return pid;
}

void stop_process(const RunningProcess& process) {
  ::kill(process.pid, SIGTERM);
  (void)::waitpid(process.pid, nullptr, 0);
}

std::vector<RunningProcess> build_processes(const Config& config) {
  std::vector<RunningProcess> processes;

  if (config.zot.enabled) {
    auto args = config.zot.args;
    if (args.empty()) {
      args = {"serve", config.zot.runtime_config.string()};
    }
    processes.push_back({"zot", start_process("zot", config.zot.binary, args)});
  }

  if (config.reposilite.enabled) {
    processes.push_back({"reposilite", start_process("reposilite",
                                                     config.reposilite.binary,
                                                     config.reposilite.args)});
  }

  return processes;
}

}  // namespace

void supervise_services(const Config& config, int supervise_seconds) {
  auto processes = build_processes(config);
  if (processes.empty()) {
    return;
  }

  const auto deadline = supervise_seconds > 0
                            ? std::chrono::steady_clock::now() +
                                  std::chrono::seconds(supervise_seconds)
                            : std::chrono::steady_clock::time_point::max();

  while (std::chrono::steady_clock::now() < deadline) {
    int status = 0;
    const pid_t exited = ::waitpid(-1, &status, WNOHANG);
    if (exited == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      continue;
    }
    if (exited < 0) {
      if (errno == ECHILD) {
        break;
      }
      for (const auto& process : processes) {
        stop_process(process);
      }
      throw std::runtime_error(std::string("failed during child wait: ") +
                               std::strerror(errno));
    }

    std::string failed_name = "child";
    for (const auto& process : processes) {
      if (process.pid == exited) {
        failed_name = process.name;
        break;
      }
    }

    for (const auto& process : processes) {
      if (process.pid != exited) {
        stop_process(process);
      }
    }

    throw std::runtime_error(failed_name + " exited unexpectedly");
  }

  for (const auto& process : processes) {
    stop_process(process);
  }
}

}  // namespace upstreamd
