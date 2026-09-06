#include "domain/logging/ai_trace.h"

#include "domain/logging/component.h"
#include "domain/logging/logger.h"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace qtrans::log {

namespace {

std::filesystem::path g_logs_dir;

}  // namespace

void configure_ai_trace(const std::filesystem::path &logs_dir) {
    g_logs_dir = logs_dir;
}

void write_ai_trace(const std::string &prompt, const std::string &response) {
#ifdef NDEBUG
    (void)prompt;
    (void)response;
    return;
#else
    if (g_logs_dir.empty()) {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(g_logs_dir, ec);
    if (ec) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &now_time);
#else
    localtime_r(&now_time, &tm_buf);
#endif

    std::ostringstream time_stream;
    time_stream << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
    const std::filesystem::path log_path = g_logs_dir / ("ai_output_" + time_stream.str() + ".log");

    std::ofstream logf(log_path, std::ios::binary);
    if (!logf.is_open()) {
        return;
    }

    logf << "=== prompt ===\n"
         << prompt << "\n\n=== response ===\n"
         << response << '\n';

    get(Component::Hymt)->info("debug ai trace written to: {}", log_path.string());
#endif
}

}  // namespace qtrans::log
