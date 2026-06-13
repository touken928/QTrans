#include "domain/inference/platform_profile.h"
#include "domain/storage/app_paths.h"
#include "domain/settings/settings.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#ifdef _WIN32
namespace {
constexpr const char kHomeEnv[] = "USERPROFILE";
}
#else
namespace {
constexpr const char kHomeEnv[] = "HOME";
}
#endif

namespace {

class AppSettingsTest : public ::testing::Test {
protected:
    void SetUp() override {
        saved_home_ = std::getenv(kHomeEnv);
        home_ = std::filesystem::temp_directory_path() /
                ("qtrans_settings_home_" + std::to_string(::getpid()) + "_" +
                 ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(home_);
        std::filesystem::create_directories(home_);
        ::setenv(kHomeEnv, home_.string().c_str(), 1);
        paths_ = AppPaths::detect(std::filesystem::temp_directory_path() / "qtrans_exec");
    }

    void TearDown() override {
        if (saved_home_) {
            ::setenv(kHomeEnv, saved_home_, 1);
        } else {
            ::unsetenv(kHomeEnv);
        }
        std::filesystem::remove_all(home_);
    }

    std::filesystem::path home_;
    const char *saved_home_ = nullptr;
    AppPaths paths_;
};

}  // namespace

TEST_F(AppSettingsTest, LoadOnMissingFileKeepsDefaults) {
    AppSettings settings;
    settings.load(paths_);
    EXPECT_EQ(settings.hotkey, "Ctrl+`");
    EXPECT_EQ(settings.auto_close_ms, 5000);
    EXPECT_EQ(settings.source_language, "English");
    EXPECT_EQ(settings.target_language, "Chinese");
    EXPECT_TRUE(settings.wordselect_enabled);
    EXPECT_TRUE(settings.models_dir.empty());
    EXPECT_EQ(settings.model_id, default_model_id_for_platform());
}

TEST_F(AppSettingsTest, SaveAndLoadRoundTrip) {
    AppSettings out;
    out.models_dir = "/tmp/qtrans-models";
    out.model_id = default_model()->id;
    out.hotkey = "Ctrl+Shift+T";
    out.auto_close_ms = 1234;
    out.source_language = "French";
    out.target_language = "Japanese";
    out.wordselect_source_language = "German";
    out.wordselect_target_language = "Korean";
    out.wordselect_enabled = false;
    out.save(paths_);

    AppSettings in;
    in.load(paths_);
    EXPECT_EQ(in.models_dir, "/tmp/qtrans-models");
    EXPECT_EQ(in.hotkey, "Ctrl+Shift+T");
    EXPECT_EQ(in.auto_close_ms, 1234);
    EXPECT_EQ(in.source_language, "French");
    EXPECT_EQ(in.target_language, "Japanese");
    EXPECT_EQ(in.wordselect_source_language, "German");
    EXPECT_EQ(in.wordselect_target_language, "Korean");
    EXPECT_FALSE(in.wordselect_enabled);
}

TEST_F(AppSettingsTest, LoadSkipsCommentsAndBlankLines) {
    paths_.ensureDirectories();
    std::ofstream(paths_.settings_file)
        << "# comment line\n"
        << "\n"
        << "hotkey=Ctrl+Alt+Z\n"
        << "   # indented comment\n"
        << "auto_close_ms=42\n";

    AppSettings settings;
    settings.load(paths_);
    EXPECT_EQ(settings.hotkey, "Ctrl+Alt+Z");
    EXPECT_EQ(settings.auto_close_ms, 42);
}

TEST_F(AppSettingsTest, LoadTrimsWhitespaceAroundKeysAndValues) {
    paths_.ensureDirectories();
    std::ofstream(paths_.settings_file) << "  hotkey  =  Ctrl+Alt+X  \n";

    AppSettings settings;
    settings.load(paths_);
    EXPECT_EQ(settings.hotkey, "Ctrl+Alt+X");
}

TEST_F(AppSettingsTest, LoadUnknownKeyIsIgnored) {
    paths_.ensureDirectories();
    std::ofstream(paths_.settings_file) << "some_future_key=foo\n"
                                        << "hotkey=Ctrl+1\n";

    AppSettings settings;
    settings.load(paths_);
    EXPECT_EQ(settings.hotkey, "Ctrl+1");
}

TEST_F(AppSettingsTest, WordselectEnabledParsesTrueAndFalse) {
    paths_.ensureDirectories();
    {
        std::ofstream(paths_.settings_file) << "wordselect_enabled=true\n";
        AppSettings s;
        s.load(paths_);
        EXPECT_TRUE(s.wordselect_enabled);
    }
    {
        std::ofstream(paths_.settings_file) << "wordselect_enabled=1\n";
        AppSettings s;
        s.load(paths_);
        EXPECT_TRUE(s.wordselect_enabled);
    }
    {
        std::ofstream(paths_.settings_file) << "wordselect_enabled=false\n";
        AppSettings s;
        s.load(paths_);
        EXPECT_FALSE(s.wordselect_enabled);
    }
    {
        std::ofstream(paths_.settings_file) << "wordselect_enabled=0\n";
        AppSettings s;
        s.load(paths_);
        EXPECT_FALSE(s.wordselect_enabled);
    }
}

TEST_F(AppSettingsTest, EffectiveModelsDirRelativeResolvedAgainstDataRoot) {
    AppSettings s;
    s.models_dir = "models";
    s.load(paths_);  // ensure migration is a no-op
    EXPECT_EQ(s.effectiveModelsDir(paths_), (paths_.data_root / "models").string());
}

TEST_F(AppSettingsTest, EffectiveModelsDirAbsoluteUsedAsIs) {
    AppSettings s;
    s.models_dir = "/absolute/path/models";
    EXPECT_EQ(s.effectiveModelsDir(paths_), "/absolute/path/models");
}

TEST_F(AppSettingsTest, EffectiveModelPathJoinsSelectedModelFilename) {
    AppSettings s;
    s.models_dir = "/abs/models";
    s.model_id = "hymt2-q4";
    const std::string path = s.effectiveModelPath(paths_);
    EXPECT_EQ(path, std::string("/abs/models/") + find_model_by_id("hymt2-q4")->filename);
}

TEST_F(AppSettingsTest, SetEffectiveModelsDirClearsWhenMatchingDefault) {
    AppSettings s;
    s.setEffectiveModelsDir(paths_, paths_.models_dir.string());
    EXPECT_TRUE(s.models_dir.empty());
}

TEST_F(AppSettingsTest, SetEffectiveModelsDirClearsWhenEmpty) {
    AppSettings s;
    s.models_dir = "something";
    s.setEffectiveModelsDir(paths_, "   ");
    EXPECT_TRUE(s.models_dir.empty());
}

TEST_F(AppSettingsTest, SetEffectiveModelsDirStoresAbsolute) {
    AppSettings s;
    s.setEffectiveModelsDir(paths_, "/abs/models");
    EXPECT_EQ(s.models_dir, "/abs/models");
}

TEST_F(AppSettingsTest, SetEffectiveModelsDirStoresRelative) {
    AppSettings s;
    s.setEffectiveModelsDir(paths_, "rel/models");
    EXPECT_EQ(s.models_dir, "rel/models");
}

TEST_F(AppSettingsTest, SetSelectedModelIdRejectsUnknown) {
    AppSettings s;
    s.model_id = default_model()->id;
    s.setSelectedModelId("nonexistent-id");
    EXPECT_EQ(s.model_id, default_model()->id);
}

TEST_F(AppSettingsTest, SetSelectedModelIdAcceptsKnown) {
    AppSettings s;
    s.setSelectedModelId(default_model()->id);
    EXPECT_EQ(s.model_id, default_model()->id);
}

TEST_F(AppSettingsTest, SelectedModelFallsBackToDefaultWhenUnknown) {
    AppSettings s;
    s.model_id = "definitely-not-a-known-id";
    const ModelCatalogEntry *entry = s.selectedModel();
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->id, default_model()->id);
}

TEST_F(AppSettingsTest, SelectedModelReturnsRequested) {
    AppSettings s;
    s.model_id = default_model()->id;
    EXPECT_EQ(s.selectedModel()->id, default_model()->id);
}

TEST_F(AppSettingsTest, LegacyModelPathMigrationSetsModelsDir) {
    paths_.ensureDirectories();
    std::ofstream(paths_.settings_file) << "model_path=/legacy/dir/Hy-MT2-1.8B-1.25Bit.gguf\n";

    AppSettings s;
    s.load(paths_);
    EXPECT_EQ(s.models_dir, "/legacy/dir");
    EXPECT_EQ(s.model_id, "hymt2-q4");
}

TEST_F(AppSettingsTest, LegacyModelPathMigrationPreservesExistingModelsDir) {
    paths_.ensureDirectories();
    std::ofstream(paths_.settings_file)
        << "models_dir=/keep\n"
        << "model_path=/legacy/dir/Hy-MT2-1.8B-1.25Bit.gguf\n";

    AppSettings s;
    s.load(paths_);
    EXPECT_EQ(s.models_dir, "/keep");
}

TEST_F(AppSettingsTest, EnsureStorageCreatesModelsDirectory) {
    AppSettings s;
    s.models_dir = "qtrans_ensure_storage";
    s.ensureStorage(paths_);
    EXPECT_TRUE(std::filesystem::exists((paths_.data_root / s.models_dir)));
}
