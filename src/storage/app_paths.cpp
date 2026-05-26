#include "storage/app_paths.h"

#include <cstdlib>
#include <system_error>

namespace {

constexpr const char kPortableMarker[] = ".portable";
constexpr const char kDefaultModelFile[] = "Hy-MT2-1.8B-1.25Bit.gguf";

std::filesystem::path homeDirectory() {
#ifdef _WIN32
    if (const char * userprofile = std::getenv("USERPROFILE"); userprofile != nullptr && userprofile[0] != '\0') {
        return std::filesystem::path(userprofile);
    }
#endif
    if (const char * home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::filesystem::path(home);
    }
    return std::filesystem::current_path();
}

} // namespace

AppPaths AppPaths::detect(const std::filesystem::path & executable_dir) {
    AppPaths paths{};
    paths.app_dir = std::filesystem::absolute(executable_dir);

    const bool portable = std::filesystem::exists(paths.app_dir / kPortableMarker);
    if (portable) {
        paths.mode = AppMode::Portable;
        paths.data_root = paths.app_dir / "data";
    } else {
        paths.mode = AppMode::System;
        paths.data_root = homeDirectory() / ".qtrans";
    }

    paths.models_dir = paths.data_root / "models";
    paths.settings_dir = paths.data_root / "settings";
    paths.settings_file = paths.settings_dir / "settings.ini";
    return paths;
}

std::string AppPaths::modeLabel() const {
    return mode == AppMode::Portable ? "Portable" : "System";
}

std::string AppPaths::defaultModelFilename() const {
    return kDefaultModelFile;
}

std::string AppPaths::defaultModelPath() const {
    return (models_dir / kDefaultModelFile).string();
}

void AppPaths::ensureDirectories() const {
    std::error_code ec;
    std::filesystem::create_directories(models_dir, ec);
    std::filesystem::create_directories(settings_dir, ec);
}
