#include "domain/logging/init.h"

#include "domain/logging/ai_trace.h"
#include "domain/logging/component.h"
#include "domain/logging/registry.h"

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <vector>

namespace qtrans::log {

namespace {

constexpr std::size_t kMaxLogFileBytes = 5 * 1024 * 1024;
constexpr std::size_t kMaxLogFiles = 3;

std::vector<spdlog::sink_ptr> make_sinks(const LogConfig &config) {
    std::vector<spdlog::sink_ptr> sinks;
    auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    console_sink->set_level(config.console_level);
    sinks.push_back(console_sink);

    if (config.enable_file_sink && !config.logs_dir.empty()) {
        const auto log_path = config.logs_dir / "qtrans.log";
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            log_path.string(),
            kMaxLogFileBytes,
            kMaxLogFiles);
        file_sink->set_level(spdlog::level::trace);
        sinks.push_back(file_sink);
    }

    return sinks;
}

void register_component(Component component, const std::vector<spdlog::sink_ptr> &sinks) {
    auto logger = std::make_shared<spdlog::logger>(component_name(component), sinks.begin(), sinks.end());
    logger->set_level(spdlog::level::trace);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
    spdlog::register_logger(logger);
    registry::set(component, logger);
}

}  // namespace

void shutdown();

void init(const LogConfig &config) {
    shutdown();
    configure_ai_trace(config.logs_dir);
    const std::vector<spdlog::sink_ptr> sinks = make_sinks(config);

    register_component(Component::App, sinks);
    register_component(Component::Hymt, sinks);
    register_component(Component::Inference, sinks);
    register_component(Component::Task, sinks);
    register_component(Component::Download, sinks);
    register_component(Component::WordSelect, sinks);
    register_component(Component::Clipboard, sinks);

    spdlog::set_default_logger(registry::find(Component::App));
}

void shutdown() {
    spdlog::shutdown();
    registry::clear();
}

}  // namespace qtrans::log
