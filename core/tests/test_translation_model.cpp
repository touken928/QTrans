#include <gtest/gtest.h>

#include "qtrans/core/translation_model.h"

using namespace qtrans::core;

namespace {

class TestLocalModel final : public LocalGgufModelBase {
public:
    TestLocalModel(std::vector<std::uint8_t> weights,
                   TranslatorOptions options,
                   PromptFormatterPtr formatter)
        : LocalGgufModelBase(std::move(weights), options),
          formatter_(std::move(formatter)) {
    }

    PromptFormatterPtr prompt_formatter() const override {
        return formatter_;
    }

private:
    PromptFormatterPtr formatter_;
};

}  // namespace

TEST(TranslationModel, LocalSubclassExposesFormatter) {
    auto formatter = std::make_shared<PromptFormatter>();
    formatter->build_user_prompt = [](const std::string &text, const std::string &) {
        return text;
    };
    formatter->format_chat_prompt = [](const std::string &user_prompt) {
        return "chat:" + user_prompt;
    };

    TestLocalModel model(std::vector<std::uint8_t>{1, 2, 3}, TranslatorOptions{}, formatter);
    EXPECT_EQ(model.prompt_formatter()->format_translation_prompt("hi", "English"), "chat:hi");
}

TEST(TranslationModel, RemoteApiRequiresFormatter) {
    auto formatter = std::make_shared<PromptFormatter>();
    formatter->build_user_prompt = [](const std::string &text, const std::string &) {
        return text;
    };
    formatter->format_chat_prompt = [](const std::string &user_prompt) {
        return user_prompt;
    };

    RemoteModelConfig remote;
    remote.endpoint_url = "https://example.com";
    EXPECT_NO_THROW(RemoteApiModel(std::move(remote), TranslatorOptions{}, formatter));
}
