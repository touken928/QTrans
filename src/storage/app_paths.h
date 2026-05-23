#pragma once

#include <filesystem>
#include <string>

enum class AppMode {
    Portable,
    System,
};

struct AppPaths {
    AppMode mode = AppMode::System;
    std::filesystem::path app_dir;
    std::filesystem::path data_root;
    std::filesystem::path models_dir;
    std::filesystem::path settings_dir;
    std::filesystem::path settings_file;

    static AppPaths detect(const std::filesystem::path & executable_dir);

    std::string modeLabel() const;
    std::string defaultModelPath() const;
    std::string defaultModelFilename() const;

    void ensureDirectories() const;
};
