#pragma once

#include "domain/logging/component.h"

#include <memory>
#include <spdlog/spdlog.h>

namespace qtrans::log::registry {

void set(Component component, std::shared_ptr<spdlog::logger> logger);
std::shared_ptr<spdlog::logger> find(Component component);
void clear();

}  // namespace qtrans::log::registry
