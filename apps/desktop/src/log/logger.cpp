#include "log/logger.h"

#include "log/registry.h"

namespace qtrans::log {

std::shared_ptr<spdlog::logger> get(Component component) {
    return registry::find(component);
}

}  // namespace qtrans::log
