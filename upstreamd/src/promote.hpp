#pragma once

#include "config.hpp"

namespace upstreamd {

void promote_once(const Config& config);
void watch_and_promote(const Config& config, int watch_seconds);

}  // namespace upstreamd
