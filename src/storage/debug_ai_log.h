#pragma once

#include <filesystem>
#include <string>

void register_app_logs_dir(const std::filesystem::path &logs_dir);

void write_debug_ai_output(const std::string &prompt, const std::string &response);
