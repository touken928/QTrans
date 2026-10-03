#pragma once

#include <filesystem>
#include <string>

namespace qtrans::log {

void configure_ai_trace(const std::filesystem::path &logs_dir);
void write_ai_trace(const std::string &prompt, const std::string &response);

}  // namespace qtrans::log
