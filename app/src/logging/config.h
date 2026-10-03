#pragma once

#include <filesystem>
#include <spdlog/common.h>

namespace qtrans::log {

struct LogConfig {
    std::filesystem::path logs_dir;
    spdlog::level::level_enum console_level = spdlog::level::trace;
    bool enable_file_sink = true;
};

}  // namespace qtrans::log
