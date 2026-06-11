#include <gtest/gtest.h>

#include "qtrans/core/cancellation.h"
#include "qtrans/core/options.h"
#include "qtrans/core/types.h"
#include "qtrans/core/translator.h"

using namespace qtrans::core;

TEST(CoreCancellation, InitiallyNotCancelled) {
    CancellationToken token;
    EXPECT_FALSE(token.is_cancelled());
    EXPECT_FALSE(token.checker()());
}

TEST(CoreCancellation, CancelSetsFlag) {
    CancellationToken token;
    token.cancel();
    EXPECT_TRUE(token.is_cancelled());
    EXPECT_TRUE(token.checker()());
}

TEST(CoreCancellation, ResetClearsFlag) {
    CancellationToken token;
    token.cancel();
    token.reset();
    EXPECT_FALSE(token.is_cancelled());
}

TEST(CoreCancellation, CheckerReflectsState) {
    CancellationToken token;
    auto check = token.checker();
    EXPECT_FALSE(check());
    token.cancel();
    EXPECT_TRUE(check());
}

TEST(CoreOptions, DefaultValues) {
    TranslatorOptions opts;
    EXPECT_EQ(opts.n_ctx, 4096);
    EXPECT_EQ(opts.max_tokens, 4096);
    EXPECT_EQ(opts.n_gpu_layers, -1);
    EXPECT_FLOAT_EQ(opts.temperature, 0.7f);
    EXPECT_EQ(opts.top_k, 20);
    EXPECT_FLOAT_EQ(opts.top_p, 0.6f);
    EXPECT_FLOAT_EQ(opts.repeat_penalty, 1.05f);

    BackendOptions back;
    EXPECT_EQ(back.backend_type, BackendType::Auto);
}

TEST(CoreOptions, BackendTypeEnum) {
    EXPECT_NE(static_cast<int>(BackendType::Auto),
              static_cast<int>(BackendType::Metal));
    EXPECT_NE(static_cast<int>(BackendType::Auto),
              static_cast<int>(BackendType::Vulkan));
}

TEST(CoreTypes, DefaultTranslationResult) {
    TranslationResult r;
    EXPECT_EQ(r.outcome, TranslationOutcome::Failed);
    EXPECT_TRUE(r.text.empty());
    EXPECT_TRUE(r.error_message.empty());
}

TEST(CoreTypes, TranslationRequestDefaults) {
    TranslationRequest req;
    EXPECT_TRUE(req.source.empty());
    EXPECT_TRUE(req.target_language.empty());
    EXPECT_FALSE(req.back_translate);
    EXPECT_FALSE(req.wordselect);
}

TEST(CoreTypes, TranslationOutcomeEnum) {
    EXPECT_NE(static_cast<int>(TranslationOutcome::Completed),
              static_cast<int>(TranslationOutcome::Cancelled));
    EXPECT_NE(static_cast<int>(TranslationOutcome::Completed),
              static_cast<int>(TranslationOutcome::Failed));
}

TEST(CoreTypes, TranslationResultCompleted) {
    TranslationResult r;
    r.outcome = TranslationOutcome::Completed;
    r.text = "hello world";
    EXPECT_EQ(r.text, "hello world");
}

TEST(CoreTypes, TranslationResultFailed) {
    TranslationResult r;
    r.outcome = TranslationOutcome::Failed;
    r.error_message = "something went wrong";
    EXPECT_EQ(r.error_message, "something went wrong");
}

TEST(CoreTypes, TranslationResultCancelled) {
    TranslationResult r;
    r.outcome = TranslationOutcome::Cancelled;
    EXPECT_TRUE(r.text.empty());
}

TEST(CoreTranslator, ConstructAndDestroy) {
    TranslatorOptions opts;
    EXPECT_NO_THROW({
        auto t = std::make_unique<Translator>(opts);
        (void)t;
    });
}

TEST(CoreRuntime, RuntimeKindEnum) {
    EXPECT_NE(static_cast<int>(RuntimeKind::Local),
              static_cast<int>(RuntimeKind::Remote));
}

TEST(CoreRuntime, RemoteModelConfigDefaults) {
    RemoteModelConfig cfg;
    EXPECT_TRUE(cfg.endpoint_url.empty());
    EXPECT_TRUE(cfg.api_key.empty());
    EXPECT_TRUE(cfg.model_name.empty());
    EXPECT_TRUE(cfg.api_provider.empty());
}

TEST(CoreRuntime, RemoteModelConfigValues) {
    RemoteModelConfig cfg;
    cfg.endpoint_url = "https://api.example.com/v1";
    cfg.api_key = "sk-test";
    cfg.model_name = "test-model";
    cfg.api_provider = "openai";
    EXPECT_EQ(cfg.endpoint_url, "https://api.example.com/v1");
    EXPECT_EQ(cfg.model_name, "test-model");
}

TEST(CoreTranslator, InjectionConstructor) {
    class MockRuntime : public ITranslationRuntime {
    public:
        void initialize_backend(const BackendOptions &) override {
        }
        void load_model(const std::vector<uint8_t> &, const TranslatorOptions &) override {
        }
        void load_remote(const RemoteModelConfig &, const TranslatorOptions &) override {
        }
        void unload() override {
        }
        bool is_loaded() const override {
            return false;
        }
        void set_prompt_formatter(PromptFormatterPtr) override {
        }
        std::string translate(const std::string &, const std::string &,
                              const std::function<void(const std::string &)> &,
                              const std::function<bool()> &) override {
            return "";
        }
        int count_prompt_tokens(const std::string &, const std::string &) const override {
            return 0;
        }
        std::string backend_label() const override {
            return "mock";
        }
        RuntimeKind kind() const override {
            return RuntimeKind::Local;
        }
    };

    TranslatorOptions opts;
    opts.n_ctx = 512;
    auto runtime = std::make_unique<MockRuntime>();
    Translator translator(std::move(runtime), opts);
    EXPECT_FALSE(translator.is_loaded());
    EXPECT_EQ(translator.backend_label(), "mock");
    EXPECT_EQ(translator.runtime_kind(), RuntimeKind::Local);
}
