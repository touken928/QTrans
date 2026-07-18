#include <gtest/gtest.h>

#include "qtrans/core/translation_model.h"

using namespace qtrans::core;

namespace {

class TestPromptStrategy final : public ITranslationPromptStrategy {
public:
    std::string build_user_prompt(std::string_view text,
                                  std::string_view /*target_language*/) const override {
        return std::string(text);
    }

    std::string format_chat_prompt(std::string_view user_prompt) const override {
        return "chat:" + std::string(user_prompt);
    }
};

}  // namespace

TEST(TranslationModel, PromptStrategyFormatsTranslationPrompt) {
    TestPromptStrategy strategy;
    EXPECT_EQ(strategy.format_translation_prompt("hi", "English"), "chat:hi");
}

TEST(TranslationModel, FunctionPromptStrategyRequiresUserPromptBuilder) {
    EXPECT_THROW(FunctionPromptStrategy(nullptr), std::invalid_argument);
}

TEST(TranslationModel, FunctionPromptStrategySupportsInferenceOverride) {
    FunctionPromptStrategy strategy(
        [](std::string_view text, std::string_view) { return std::string("user:") + std::string(text); },
        [](std::string_view user_prompt) { return std::string("chat:") + std::string(user_prompt); },
        [](std::string_view text, std::string_view) { return std::string("infer:") + std::string(text); });
    EXPECT_EQ(strategy.format_inference_prompt("hi", "English"), "infer:hi");
    EXPECT_EQ(strategy.format_translation_prompt("hi", "English"), "chat:user:hi");
}

// ── TranslationProfile validation ──────────────────────────────────────────

TEST(TranslationProfile, RejectsNullPromptStrategy) {
    TranslationProfile p;
    LocalModelConfig local;
    local.weights = std::vector<std::uint8_t>{1, 2, 3};
    p.model = std::move(local);
    p.options.context.n_ctx = 4096;
    p.options.context.max_tokens = 4096;
    auto err = p.validate();
    ASSERT_TRUE(err.has_value());
    EXPECT_NE(err->find("prompt strategy"), std::string::npos);
}

TEST(TranslationProfile, RejectsEmptyLocalSources) {
    auto prompt = std::make_shared<FunctionPromptStrategy>(
        [](std::string_view, std::string_view) { return std::string(); });
    TranslationProfile p;
    p.model = LocalModelConfig{};
    p.prompt_strategy = prompt;
    p.options.context.n_ctx = 4096;
    p.options.context.max_tokens = 4096;
    auto err = p.validate();
    ASSERT_TRUE(err.has_value());
    EXPECT_NE(err->find("local model source is empty"), std::string::npos);
}

TEST(TranslationProfile, RejectsEmptyLocalPathAndWeights) {
    auto prompt = std::make_shared<FunctionPromptStrategy>(
        [](std::string_view, std::string_view) { return std::string(); });
    TranslationProfile p;
    LocalModelConfig local;
    local.path = std::filesystem::path{};
    local.weights.clear();
    p.model = local;
    p.prompt_strategy = prompt;
    p.options.context.n_ctx = 4096;
    p.options.context.max_tokens = 4096;
    auto err = p.validate();
    ASSERT_TRUE(err.has_value());
    EXPECT_NE(err->find("local model source is empty"), std::string::npos);
}

TEST(TranslationProfile, AcceptsValidLocalPathProfile) {
    auto prompt = std::make_shared<FunctionPromptStrategy>(
        [](std::string_view, std::string_view) { return std::string(); });
    TranslationProfile p;
    LocalModelConfig local;
    local.path = "/some/model.gguf";
    p.model = local;
    p.prompt_strategy = prompt;
    p.options.context.n_ctx = 4096;
    p.options.context.max_tokens = 4096;
    EXPECT_FALSE(p.validate().has_value());
}

TEST(TranslationProfile, AcceptsValidLocalWeightsProfile) {
    auto prompt = std::make_shared<FunctionPromptStrategy>(
        [](std::string_view, std::string_view) { return std::string(); });
    TranslationProfile p;
    LocalModelConfig local;
    local.weights = std::vector<std::uint8_t>{1, 2, 3};
    p.model = std::move(local);
    p.prompt_strategy = prompt;
    p.options.context.n_ctx = 4096;
    p.options.context.max_tokens = 4096;
    EXPECT_FALSE(p.validate().has_value());
}

TEST(TranslationProfile, RejectsEmptyRemoteEndpoint) {
    auto prompt = std::make_shared<FunctionPromptStrategy>(
        [](std::string_view, std::string_view) { return std::string(); });
    TranslationProfile p;
    p.model = RemoteModelConfig{};
    p.prompt_strategy = prompt;
    p.options.context.n_ctx = 4096;
    p.options.context.max_tokens = 4096;
    auto err = p.validate();
    ASSERT_TRUE(err.has_value());
    EXPECT_NE(err->find("endpoint URL"), std::string::npos);
}

TEST(TranslationProfile, RejectsEmptyRemoteModelName) {
    auto prompt = std::make_shared<FunctionPromptStrategy>(
        [](std::string_view, std::string_view) { return std::string(); });
    RemoteModelConfig remote;
    remote.endpoint_url = "https://example.com";
    TranslationProfile p;
    p.model = remote;
    p.prompt_strategy = prompt;
    p.options.context.n_ctx = 4096;
    p.options.context.max_tokens = 4096;
    auto err = p.validate();
    ASSERT_TRUE(err.has_value());
    EXPECT_NE(err->find("model name is empty"), std::string::npos);
}

TEST(TranslationProfile, RejectsNonPositiveNctx) {
    auto prompt = std::make_shared<FunctionPromptStrategy>(
        [](std::string_view, std::string_view) { return std::string(); });
    RemoteModelConfig remote;
    remote.endpoint_url = "https://example.com";
    remote.model_name = "test";
    TranslationProfile p;
    p.model = remote;
    p.prompt_strategy = prompt;
    p.options.context.n_ctx = 0;
    auto err = p.validate();
    ASSERT_TRUE(err.has_value());
    EXPECT_NE(err->find("n_ctx"), std::string::npos);
}

TEST(TranslationProfile, AcceptsValidLocalProfile) {
    auto prompt = std::make_shared<FunctionPromptStrategy>(
        [](std::string_view, std::string_view) { return std::string(); });
    TranslationProfile p;
    LocalModelConfig local;
    local.weights = std::vector<std::uint8_t>{1, 2, 3};
    p.model = std::move(local);
    p.prompt_strategy = prompt;
    p.options.context.n_ctx = 4096;
    p.options.context.max_tokens = 4096;
    EXPECT_FALSE(p.validate().has_value());
}

TEST(TranslationProfile, AcceptsValidRemoteProfile) {
    auto prompt = std::make_shared<FunctionPromptStrategy>(
        [](std::string_view, std::string_view) { return std::string(); });
    RemoteModelConfig remote;
    remote.endpoint_url = "https://example.com";
    remote.model_name = "test";
    TranslationProfile p;
    p.model = remote;
    p.prompt_strategy = prompt;
    p.options.context.n_ctx = 4096;
    p.options.context.max_tokens = 4096;
    EXPECT_FALSE(p.validate().has_value());
}
