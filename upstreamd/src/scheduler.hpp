#pragma once

#include "config.hpp"

namespace upstreamd {

void run_sync_once(const Config& config, const std::string& target_name);
void run_scheduler(const Config& config, int run_seconds);

}  // namespace upstreamd
