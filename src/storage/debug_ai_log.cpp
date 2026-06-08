#include "storage/debug_ai_log.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>

namespace {

std::mutex g_logs_mutex;
std::filesystem::path g_logs_dir;

}  // namespace

void register_app_logs_dir(const std::filesystem::path &logs_dir) {
    std::lock_guard<std::mutex> lock(g_logs_mutex);
    g_logs_dir = logs_dir;
}

void write_debug_ai_output(const std::string &prompt, const std::string &response) {
#ifndef NDEBUG
    std::filesystem::path logs_dir;
    {
        std::lock_guard<std::mutex> lock(g_logs_mutex);
        logs_dir = g_logs_dir;
    }
    if (logs_dir.empty()) {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(logs_dir, ec);

    time_t now = time(nullptr);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S", localtime(&now));

    const std::filesystem::path log_path = logs_dir / ("ai_output_" + std::string(time_buf) + ".log");
    std::ofstream logf(log_path, std::ios::binary);
    if (!logf.is_open()) {
        return;
    }

    logf << "=== prompt ===\n"
         << prompt << "\n\n=== response ===\n"
         << response << '\n';
    fprintf(stderr, "[Hymt] debug log written to: %s\n", log_path.string().c_str());
#endif
}
