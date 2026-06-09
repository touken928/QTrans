#pragma once

#include "log/config.h"

namespace qtrans::log {

void init(const LogConfig &config);
void shutdown();

}  // namespace qtrans::log
