#include "models/local/hymt2_18b_local_model.h"
#include "models/local/hymt2_7b_local_model.h"
#include "models/local/local_model_factory.h"
#include "catalog/model_catalog.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
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
    const std::unique_ptr<HyMt2_18BLocalModel> model = HyMt2_18BLocalModel::from_path(path, -1);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->translator_options().n_ctx, 4096);
    EXPECT_EQ(model->translator_options().max_tokens, 4096);
    EXPECT_EQ(model->translator_options().n_gpu_layers, -1);

    const std::string user = model->prompt_formatter()->build_user_prompt("Hello", "Chinese");
    const std::string chat = model->prompt_formatter()->format_translation_prompt("Hello", "Chinese");
    EXPECT_NE(chat.find(u8"<\xEF\xBD\x9Chy_User\xEF\xBD\x9C>"), std::string::npos);
    EXPECT_EQ(chat, model->prompt_formatter()->format_chat_prompt(user));
    std::filesystem::remove(path);
}

TEST(HyMt2_7BLocalModel, SetsContextAndChatTemplate) {
    const std::filesystem::path path = write_temp_bytes({0x47, 0x47, 0x55, 0x46});
    const std::unique_ptr<HyMt2_7BLocalModel> model = HyMt2_7BLocalModel::from_path(path, -1);
    ASSERT_NE(model, nullptr);
    EXPECT_EQ(model->translator_options().n_ctx, 8192);
    EXPECT_EQ(model->translator_options().max_tokens, 8192);

    const std::string user = model->prompt_formatter()->build_user_prompt("Hello", "Chinese");
    const std::string chat = model->prompt_formatter()->format_translation_prompt("Hello", "Chinese");
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
