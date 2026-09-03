#include "domain/storage/app_paths.h"
#include "../test_environment.h"

#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#ifdef _WIN32
namespace {
constexpr const char kHomeEnv[] = "USERPROFILE";
constexpr const char kHomeAltEnv[] = "HOME";
}  // namespace
#else
namespace {
constexpr const char kHomeEnv[] = "HOME";
constexpr const char kHomeAltEnv[] = "USERPROFILE";
}  // namespace
#endif

class AppPathsDetect : public ::testing::Test {
protected:
    void SetUp() override {
        saved_home_ = test_support::read_environment(kHomeEnv);
        saved_alt_ = test_support::read_environment(kHomeAltEnv);
    }

    void TearDown() override {
        test_support::restore_environment(kHomeEnv, saved_home_);
        test_support::restore_environment(kHomeAltEnv, saved_alt_);
    }

    test_support::EnvironmentValue saved_home_;
    test_support::EnvironmentValue saved_alt_;
};

TEST_F(AppPathsDetect, PortableModeWhenMarkerExists) {
    auto tmp = std::filesystem::temp_directory_path() /
               ("qtrans_app_paths_portable_" +
                std::to_string(test_support::current_pid()));
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    std::ofstream(tmp / ".portable").close();

    const AppPaths paths = AppPaths::detect(tmp);
    EXPECT_EQ(paths.mode, AppMode::Portable);
    EXPECT_EQ(paths.data_root, std::filesystem::absolute(tmp) / "data");
    EXPECT_EQ(paths.models_dir, std::filesystem::absolute(tmp) / "data" / "models");
    EXPECT_EQ(paths.settings_dir, std::filesystem::absolute(tmp) / "data" / "settings");
    EXPECT_EQ(paths.logs_dir, std::filesystem::absolute(tmp) / "data" / "logs");
    EXPECT_EQ(paths.settings_file.filename(), "settings.ini");

    std::filesystem::remove_all(tmp);
}

TEST_F(AppPathsDetect, SystemModeUsesHomeDirectory) {
    const auto home = std::filesystem::temp_directory_path() / "qtrans_test_home";
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home);
    test_support::set_environment(kHomeEnv, home.string());
    test_support::set_environment(kHomeAltEnv, home.string());

    const auto exec_dir = std::filesystem::temp_directory_path() / "qtrans_exec_dir";
    std::filesystem::remove_all(exec_dir);
    std::filesystem::create_directories(exec_dir);

    const AppPaths paths = AppPaths::detect(exec_dir);
    EXPECT_EQ(paths.mode, AppMode::System);
    EXPECT_EQ(paths.data_root, home / ".qtrans");
    EXPECT_EQ(paths.models_dir, home / ".qtrans" / "models");
    EXPECT_EQ(paths.logs_dir, home / ".qtrans" / "logs");
    EXPECT_EQ(paths.settings_file, home / ".qtrans" / "settings" / "settings.ini");

    std::filesystem::remove_all(home);
    std::filesystem::remove_all(exec_dir);
}

TEST_F(AppPathsDetect, DefaultModelPathUnderModelsDir) {
    const auto home = std::filesystem::temp_directory_path() / "qtrans_test_home2";
    std::filesystem::remove_all(home);
    std::filesystem::create_directories(home);
    test_support::set_environment(kHomeEnv, home.string());
    test_support::set_environment(kHomeAltEnv, home.string());

    const auto exec_dir = std::filesystem::temp_directory_path() / "qtrans_exec_dir2";
    std::filesystem::remove_all(exec_dir);
    std::filesystem::create_directories(exec_dir);

    const AppPaths paths = AppPaths::detect(exec_dir);
    EXPECT_EQ(paths.defaultModelFilename(), "Hy-MT2-1.8B-Q4_K_M.gguf");
    EXPECT_EQ(paths.defaultModelPath(), (paths.models_dir / "Hy-MT2-1.8B-Q4_K_M.gguf").string());
    EXPECT_EQ(paths.modeLabel(), "System");

    std::filesystem::remove_all(home);
    std::filesystem::remove_all(exec_dir);
}

TEST(AppPaths, EnsureDirectoriesIsIdempotent) {
    const auto base = std::filesystem::temp_directory_path() /
                      ("qtrans_ensure_" +
                       std::to_string(test_support::current_pid()));
    std::filesystem::remove_all(base);
    AppPaths paths{};
    paths.models_dir = base / "models";
    paths.settings_dir = base / "settings";
    paths.logs_dir = base / "logs";
    paths.settings_file = paths.settings_dir / "settings.ini";

    paths.ensureDirectories();
    paths.ensureDirectories();  // must not throw

    EXPECT_TRUE(std::filesystem::exists(paths.models_dir));
    EXPECT_TRUE(std::filesystem::exists(paths.settings_dir));
    EXPECT_TRUE(std::filesystem::exists(paths.logs_dir));

    std::filesystem::remove_all(base);
}
