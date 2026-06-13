#include "domain/model-adapters/hymt2_18b_local_model.h"
#include "domain/model-adapters/hymt2_7b_local_model.h"
#include "domain/model-adapters/local_model_factory.h"
#include "domain/model-catalog/model_catalog.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <variant>
#include <vector>

namespace {

std::filesystem::path write_temp_bytes(const std::vector<std::uint8_t> &data) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("qtrans-test-weights-" + std::to_string(std::rand()) + ".gguf");
    std::FILE *file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr) {
        throw std::runtime_error("failed to create temp gguf file");
    }
    std::fwrite(data.data(), 1, data.size(), file);
    std::fclose(file);
    return path;
}

}  // namespace

TEST(HyMt2_18BLocalModel, SetsContextAndChatTemplate) {
    const std::filesystem::path path = write_temp_bytes({0x47, 0x47, 0x55, 0x46});
    const qtrans::core::TranslationProfile profile = make_hymt2_18b_local_profile(path, -1);
    const auto *local = std::get_if<qtrans::core::LocalModelConfig>(&profile.model);
    ASSERT_NE(profile.prompt_strategy, nullptr);
    ASSERT_NE(local, nullptr);
    EXPECT_EQ(local->path, path);
    EXPECT_TRUE(local->weights.empty());
    EXPECT_EQ(profile.options.context.n_ctx, 4096);
    EXPECT_EQ(profile.options.context.max_tokens, 4096);
    EXPECT_EQ(profile.options.n_gpu_layers, -1);

    const std::string user = profile.prompt_strategy->build_user_prompt("Hello", "Chinese");
    const std::string chat = profile.prompt_strategy->format_translation_prompt("Hello", "Chinese");
    EXPECT_NE(chat.find(u8"<\xEF\xBD\x9Chy_User\xEF\xBD\x9C>"), std::string::npos);
    EXPECT_EQ(chat, profile.prompt_strategy->format_chat_prompt(user));
    std::filesystem::remove(path);
}

TEST(HyMt2_7BLocalModel, SetsContextAndChatTemplate) {
    const std::filesystem::path path = write_temp_bytes({0x47, 0x47, 0x55, 0x46});
    const qtrans::core::TranslationProfile profile = make_hymt2_7b_local_profile(path, -1);
    const auto *local = std::get_if<qtrans::core::LocalModelConfig>(&profile.model);
    ASSERT_NE(profile.prompt_strategy, nullptr);
    ASSERT_NE(local, nullptr);
    EXPECT_EQ(local->path, path);
    EXPECT_TRUE(local->weights.empty());
    EXPECT_EQ(profile.options.context.n_ctx, 8192);
    EXPECT_EQ(profile.options.context.max_tokens, 8192);

    const std::string user = profile.prompt_strategy->build_user_prompt("Hello", "Chinese");
    const std::string chat = profile.prompt_strategy->format_translation_prompt("Hello", "Chinese");
    EXPECT_EQ(chat, "<|startoftext|>" + user + "<|extra_0|>");
    std::filesystem::remove(path);
}

TEST(LocalModelFactory, UnknownIdThrows) {
    ModelCatalogEntry entry{};
    entry.id = "unsupported-model";
    EXPECT_THROW(create_local_model(entry, "/tmp/unused.gguf", -1), std::runtime_error);
}

TEST(LocalModelFactory, ResolvesCatalogModelsIndependently) {
    const ModelCatalogEntry *small = find_model_by_id("hymt2-q4");
    const ModelCatalogEntry *large = find_model_by_id("hymt2-7b-q4");
    ASSERT_NE(small, nullptr);
    ASSERT_NE(large, nullptr);
    EXPECT_NE(small->id, large->id);
}
