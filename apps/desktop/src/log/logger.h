#pragma once

#include "log/component.h"

#include <memory>
#include <spdlog/spdlog.h>

namespace qtrans::log {

std::shared_ptr<spdlog::logger> get(Component component);

}  // namespace qtrans::log
