#include "log/component.h"
#include "log/init.h"
#include "log/logger.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <spdlog/common.h>

TEST(LogInit, RegistersComponentLoggers) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "qtrans_log_test";
    std::filesystem::create_directories(temp_dir);

    qtrans::log::LogConfig config;
    config.logs_dir = temp_dir;
    config.console_level = spdlog::level::trace;
    config.enable_file_sink = true;

    qtrans::log::init(config);

    const auto hymt = qtrans::log::get(qtrans::log::Component::Hymt);
    ASSERT_NE(hymt, nullptr);
    EXPECT_STREQ(hymt->name().c_str(), "hymt");
    EXPECT_NO_THROW(hymt->trace("log init test"));

    qtrans::log::shutdown();
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
}

TEST(LogInit, ReinitializesSafely) {
    const auto temp_dir = std::filesystem::temp_directory_path() / "qtrans_log_reinit_test";
    std::filesystem::create_directories(temp_dir);

    qtrans::log::LogConfig config;
    config.logs_dir = temp_dir;
    config.console_level = spdlog::level::info;
    config.enable_file_sink = false;

    EXPECT_NO_THROW(qtrans::log::init(config));
    EXPECT_NO_THROW(qtrans::log::init(config));

    const auto task = qtrans::log::get(qtrans::log::Component::Task);
    ASSERT_NE(task, nullptr);
    EXPECT_STREQ(task->name().c_str(), "task");

    qtrans::log::shutdown();
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
}
