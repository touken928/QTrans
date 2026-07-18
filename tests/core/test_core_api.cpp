#include <gtest/gtest.h>

#include "qtrans/core/cancellation.h"
#include "qtrans/core/backend_environment.h"
#include "qtrans/core/options.h"
#include "qtrans/core/translation_model.h"
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
    EXPECT_EQ(opts.context.n_ctx, 4096);
    EXPECT_EQ(opts.context.max_tokens, 4096);
    EXPECT_EQ(opts.n_gpu_layers, -1);
    EXPECT_FLOAT_EQ(opts.generation.temperature, 0.7f);
    EXPECT_EQ(opts.generation.top_k, 20);
    EXPECT_FLOAT_EQ(opts.generation.top_p, 0.6f);
    EXPECT_FLOAT_EQ(opts.generation.repeat_penalty, 1.05f);

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
        void load(const ModelLoadSpec &, const TranslatorOptions &) override {
        }
        void unload() override {
        }
        bool is_loaded() const override {
            return false;
        }
        std::string translate(const std::string &,
                              const std::function<void(const std::string &)> &,
                              const std::function<bool()> &) override {
            return "";
        }
        int count_prompt_tokens(const std::string &) const override {
            return 0;
        }
        std::string backend_label() const override {
            return "mock";
        }
        RuntimeKind kind() const override {
            return RuntimeKind::Local;
        }
        RuntimeTraits traits() const override {
            RuntimeTraits t;
            t.kind = RuntimeKind::Local;
            return t;
        }
    };

    TranslatorOptions opts;
    opts.context.n_ctx = 512;
    auto runtime = std::make_unique<MockRuntime>();
    Translator translator(std::move(runtime), opts);
    EXPECT_FALSE(translator.is_loaded());
    EXPECT_EQ(translator.backend_label(), "mock");
    EXPECT_EQ(translator.runtime_kind(), RuntimeKind::Local);
}

TEST(CorePromptStrategy, FunctionStrategyFormatsPrompt) {
    FunctionPromptStrategy strategy(
        [](std::string_view text, std::string_view) { return std::string("user:") + std::string(text); },
        [](std::string_view user_prompt) { return std::string("chat:") + std::string(user_prompt); },
        [](std::string_view text, std::string_view) { return std::string("infer:") + std::string(text); });

    EXPECT_EQ(strategy.build_user_prompt("hi", "English"), "user:hi");
    EXPECT_EQ(strategy.format_translation_prompt("hi", "English"), "chat:user:hi");
    EXPECT_EQ(strategy.format_inference_prompt("hi", "English"), "infer:hi");
}

TEST(CoreBackendEnvironment, BackendLabelAccessible) {
    const auto &environment = BackendEnvironment::initialize_and_resolve({});
    EXPECT_TRUE(environment.initialized);
    EXPECT_FALSE(environment.label.empty());
}

TEST(CoreBackendEnvironment, ResolveApiProvidesCapabilitiesAndLabel) {
    const auto &resolved = BackendEnvironment::current();
    EXPECT_FALSE(resolved.label.empty());
    EXPECT_TRUE(resolved.label == "CPU" || resolved.label == "Metal" || resolved.label == "Vulkan");
}

TEST(CoreTranslator, ExplicitUnavailableBackendFailsLoad) {
    BackendOptions backend_options;
    const auto &environment = BackendEnvironment::initialize_and_resolve({});
    BackendType explicit_backend = BackendType::Auto;
    if (!environment.capabilities.vulkan_available) {
        explicit_backend = BackendType::Vulkan;
    } else if (!environment.capabilities.metal_available) {
        explicit_backend = BackendType::Metal;
    }

    if (explicit_backend == BackendType::Auto) {
        GTEST_SKIP() << "No unavailable explicit backend available on this platform";
    }

    backend_options.backend_type = explicit_backend;
    Translator translator(environment, backend_options, TranslatorOptions{});

    TranslationProfile profile;
    LocalModelConfig local;
    local.weights = {1, 2, 3};
    profile.model = local;
    profile.prompt_strategy = std::make_shared<FunctionPromptStrategy>(
        [](std::string_view text, std::string_view) { return std::string(text); });

    EXPECT_THROW(translator.load(std::move(profile)), std::runtime_error);
}

TEST(CoreRuntime, FactoryInterface) {
    class MockFactory : public ITranslationRuntimeFactory {
    public:
        std::unique_ptr<ITranslationRuntime> create_runtime(
            const ModelLoadSpec &) override {
            struct NullRuntime : ITranslationRuntime {
                void load(const ModelLoadSpec &, const TranslatorOptions &) override {
                }
                void unload() override {
                }
                bool is_loaded() const override {
                    return false;
                }
                std::string translate(
                    const std::string &,
                    const std::function<void(const std::string &)> &,
                    const std::function<bool()> &) override {
                    return {};
                }
                int count_prompt_tokens(const std::string &) const override {
                    return 0;
                }
                std::string backend_label() const override {
                    return "factory-made";
                }
                RuntimeKind kind() const override {
                    return RuntimeKind::Local;
                }
                RuntimeTraits traits() const override {
                    RuntimeTraits t;
                    t.kind = RuntimeKind::Local;
                    return t;
                }
            };
            return std::make_unique<NullRuntime>();
        }
    };

    auto factory = std::make_unique<MockFactory>();
    Translator translator(std::move(factory), TranslatorOptions{});
    TranslationProfile profile;
    RemoteModelConfig remote;
    remote.endpoint_url = "https://example.com";
    remote.model_name = "test";
    profile.model = remote;
    profile.prompt_strategy = std::make_shared<FunctionPromptStrategy>(
        [](std::string_view, std::string_view) { return std::string(); });
    translator.load(std::move(profile));
    EXPECT_EQ(translator.backend_label(), "factory-made");
}

// ── Traits-driven Translator behavior ──────────────────────────────────────

TEST(CoreTranslator, RuntimeManagedTraitsSkipsLocalContextEnforcement) {
    class TraitsMock : public ITranslationRuntime {
    public:
        mutable bool count_called = false;
        bool loaded = true;
        RuntimeTraits traits_;

        TraitsMock(RuntimeTraits t)
            : traits_(std::move(t)) {
        }

        void load(const ModelLoadSpec &, const TranslatorOptions &) override {
        }
        void unload() override {
        }
        bool is_loaded() const override {
            return loaded;
        }

        std::string translate(
            const std::string &prompt,
            const std::function<void(const std::string &)> &,
            const std::function<bool()> &) override {
            return "translated:" + prompt;
        }

        int count_prompt_tokens(const std::string &) const override {
            count_called = true;
            return 999999;
        }

        std::string backend_label() const override {
            return "traits";
        }
        RuntimeKind kind() const override {
            return traits_.kind;
        }
        RuntimeTraits traits() const override {
            return traits_;
        }
    };

    RuntimeTraits managed;
    managed.kind = RuntimeKind::Remote;
    managed.context_handling = ContextHandling::RuntimeManaged;
    managed.streaming = StreamingSupport::FullResultCallback;

    auto mock = std::make_unique<TraitsMock>(managed);
    TraitsMock *raw = mock.get();
    Translator translator(std::move(mock), TranslatorOptions{});
    TranslationProfile profile;
    RemoteModelConfig remote;
    remote.endpoint_url = "https://example.com";
    remote.model_name = "test";
    profile.model = remote;
    profile.prompt_strategy = std::make_shared<FunctionPromptStrategy>(
        [](std::string_view text, std::string_view) { return std::string("prompt:") + std::string(text); });
    translator.load(std::move(profile));

    TranslationRequest req;
    req.source = "very long text that would exceed context if checked";
    req.target_language = "Chinese";
    req.wordselect = true;

    TranslationResult result = translator.translate(req);
    EXPECT_EQ(result.outcome, TranslationOutcome::Completed);
    EXPECT_FALSE(result.text.empty());
    // count_prompt_tokens should NOT be called for RuntimeManaged
    EXPECT_FALSE(raw->count_called);
}

TEST(CoreTranslator, LocalEnforcedTraitsChecksContextAndRejectsWordselectOverflow) {
    class ContextMock : public ITranslationRuntime {
    public:
        bool loaded = true;
        RuntimeTraits traits_;

        ContextMock(RuntimeTraits t)
            : traits_(std::move(t)) {
        }

        void load(const ModelLoadSpec &, const TranslatorOptions &) override {
        }
        void unload() override {
        }
        bool is_loaded() const override {
            return loaded;
        }

        std::string translate(
            const std::string &,
            const std::function<void(const std::string &)> &,
            const std::function<bool()> &) override {
            return "unexpected";
        }

        int count_prompt_tokens(const std::string &prompt) const override {
            return static_cast<int>(prompt.size()) * 10;
        }

        std::string backend_label() const override {
            return "local-traits";
        }
        RuntimeKind kind() const override {
            return traits_.kind;
        }
        RuntimeTraits traits() const override {
            return traits_;
        }
    };

    RuntimeTraits enforced;
    enforced.kind = RuntimeKind::Local;
    enforced.context_handling = ContextHandling::LocalEnforced;
    enforced.has_precise_token_counting = true;

    auto mock = std::make_unique<ContextMock>(enforced);
    Translator translator(std::move(mock), TranslatorOptions{});
    TranslationProfile profile;
    LocalModelConfig local;
    local.weights = {1, 2, 3};
    profile.model = local;
    profile.options.context.n_ctx = 512;
    profile.options.context.max_tokens = 512;
    profile.prompt_strategy = std::make_shared<FunctionPromptStrategy>(
        [](std::string_view text, std::string_view) { return std::string(text); });
    translator.load(std::move(profile));

    TranslationRequest req;
    req.source = std::string(200, 'x');
    req.target_language = "Chinese";
    req.wordselect = true;

    TranslationResult result = translator.translate(req);
    ASSERT_EQ(result.outcome, TranslationOutcome::Failed);
    EXPECT_NE(result.error_message.find("context limit"), std::string::npos);
}
