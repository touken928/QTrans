#include "domain/logging/registry.h"

#include <mutex>
#include <unordered_map>

namespace qtrans::log::registry {

namespace {

std::mutex g_mutex;
std::unordered_map<Component, std::shared_ptr<spdlog::logger>> g_loggers;

}  // namespace

void set(Component component, std::shared_ptr<spdlog::logger> logger) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_loggers[component] = std::move(logger);
}

std::shared_ptr<spdlog::logger> find(Component component) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_loggers.find(component);
    if (it != g_loggers.end()) {
        return it->second;
    }
    return spdlog::default_logger();
}

void clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_loggers.clear();
}

}  // namespace qtrans::log::registry
