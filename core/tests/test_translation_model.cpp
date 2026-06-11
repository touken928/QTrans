#include <gtest/gtest.h>

#include "qtrans/core/translation_model.h"

using namespace qtrans::core;

namespace {

class TestLocalModel final : public LocalGgufModelBase {
public:
    TestLocalModel(std::vector<std::uint8_t> weights, TranslatorOptions options)
        : LocalGgufModelBase(std::move(weights), options) {
    }

    std::string build_user_prompt(const std::string &text,
                                  const std::string & /*target_language*/) const override {
        return text;
    }

    std::string format_chat_prompt(const std::string &user_prompt) const override {
        return "chat:" + user_prompt;
    }
};

}  // namespace

TEST(TranslationModel, LocalSubclassFormatsTranslationPrompt) {
    TestLocalModel model(std::vector<std::uint8_t>{1, 2, 3}, TranslatorOptions{});
    EXPECT_EQ(model.format_translation_prompt("hi", "English"), "chat:hi");
}

TEST(TranslationModel, RemoteApiRequiresUserPromptBuilder) {
    RemoteModelConfig remote;
    remote.endpoint_url = "https://example.com";
    EXPECT_THROW(RemoteApiModel(std::move(remote), TranslatorOptions{}, nullptr), std::invalid_argument);
}

TEST(TranslationModel, RemoteApiUsesUserPromptForInference) {
    RemoteModelConfig remote;
    remote.endpoint_url = "https://example.com";
    RemoteApiModel model(
        std::move(remote),
        TranslatorOptions{},
        [](const std::string &text, const std::string &) { return "user:" + text; },
        [](const std::string &user_prompt) { return "chat:" + user_prompt; });
    EXPECT_EQ(model.format_inference_prompt("hi", "English"), "user:hi");
    EXPECT_EQ(model.format_translation_prompt("hi", "English"), "chat:user:hi");
}
