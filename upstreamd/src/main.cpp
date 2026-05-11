#include "config.hpp"
#include "layout.hpp"
#include "promote.hpp"
#include "runtime.hpp"
#include "scheduler.hpp"
#include "supervisor.hpp"
#include "validate.hpp"

#include <exception>
#include <iostream>

int main(int argc, char* argv[]) {
  try {
    if (argc < 2 || argc > 4) {
      std::cerr << "usage: upstreamd <config.toml> [--promote-once | --watch [seconds] | --supervise [seconds] | --run-scheduler [seconds] | --run-sync <repositories|registries>]\n";
      return 2;
    }

    const auto config = upstreamd::load_config(argv[1]);
    upstreamd::ensure_layout(config);
    upstreamd::validate_startup(config);
    upstreamd::write_zot_runtime_config(config);
    if (argc >= 3) {
      const std::string mode = argv[2];
      if (mode == "--promote-once") {
        upstreamd::promote_once(config);
      } else if (mode == "--watch") {
        int watch_seconds = 0;
        if (argc == 4) {
          watch_seconds = std::stoi(argv[3]);
        }
        upstreamd::watch_and_promote(config, watch_seconds);
      } else if (mode == "--supervise") {
        int supervise_seconds = 0;
        if (argc == 4) {
          supervise_seconds = std::stoi(argv[3]);
        }
        upstreamd::supervise_services(config, supervise_seconds);
      } else if (mode == "--run-scheduler") {
        int run_seconds = 0;
        if (argc == 4) {
          run_seconds = std::stoi(argv[3]);
        }
        upstreamd::run_scheduler(config, run_seconds);
      } else if (mode == "--run-sync") {
        if (argc != 4) {
          std::cerr << "usage: upstreamd <config.toml> [--promote-once | --watch [seconds] | --supervise [seconds] | --run-scheduler [seconds] | --run-sync <repositories|registries>]\n";
          return 2;
        }
        upstreamd::run_sync_once(config, argv[3]);
      } else {
        std::cerr << "usage: upstreamd <config.toml> [--promote-once | --watch [seconds] | --supervise [seconds] | --run-scheduler [seconds] | --run-sync <repositories|registries>]\n";
        return 2;
      }
    }
    upstreamd::print_summary(config);
    return 0;
  } catch (const std::exception& ex) {
    std::cerr << "upstreamd: " << ex.what() << '\n';
    return 1;
  }
}
