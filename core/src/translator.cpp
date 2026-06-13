#include "qtrans/core/translator.h"

#include "diagnostics.h"
#include "runtime/default_runtime_factory.h"
#include "text/chunker.h"

#include <algorithm>
#include <stdexcept>

namespace qtrans::core {

namespace {

bool backend_request_available(const BackendOptions &options,
                               const ResolvedBackendEnvironment &environment) {
    switch (options.backend_type) {
        case BackendType::Metal:
            return environment.capabilities.metal_available;
        case BackendType::Vulkan:
            return environment.capabilities.vulkan_available;
        case BackendType::Auto:
        default:
            return true;
    }
}

std::string unavailable_backend_message(const BackendOptions &options) {
    switch (options.backend_type) {
        case BackendType::Metal:
            return "requested backend Metal is unavailable";
        case BackendType::Vulkan:
            return "requested backend Vulkan is unavailable";
        case BackendType::Auto:
        default:
            return "requested backend is unavailable";
    }
}

TranslationResult make_failure(const std::string &message) {
    TranslationResult result;
    result.outcome = TranslationOutcome::Failed;
    result.error_message = message;
    return result;
}

TranslationResult make_cancelled() {
    TranslationResult result;
    result.outcome = TranslationOutcome::Cancelled;
    return result;
}

TranslationResult make_completed(std::string text) {
    TranslationResult result;
    result.outcome = TranslationOutcome::Completed;
    result.text = std::move(text);
    return result;
}

std::function<bool()> cancel_checker(const CancellationToken *cancel) {
    return cancel != nullptr ? cancel->checker() : std::function<bool()>();
}

int max_chunk_prompt_tokens(const TranslatorOptions &config) {
    const int output_room = std::max(kTranslationOutputReserve, config.context.n_ctx / 2);
    const int budget = config.context.n_ctx - output_room;
    return budget > 0 ? budget : 1;
}

bool prompt_fits_context(int prompt_tokens, const TranslatorOptions &config) {
    return prompt_tokens > 0 && prompt_tokens + kTranslationOutputReserve <= config.context.n_ctx;
}

std::string context_limit_error() {
    return "selected text exceeds the model context limit; select a shorter passage";
}

TranslationResult translate_single_chunk(
    ITranslationRuntime &runtime,
    const ITranslationPromptStrategy &prompt_strategy,
    const std::string &text,
    const std::string &target_language,
    const std::function<void(const std::string &)> &on_token,
    const std::function<bool()> &should_cancel) {
    try {
        const std::string prompt = prompt_strategy.format_inference_prompt(text, target_language);
        const std::string result = runtime.translate(prompt, on_token, should_cancel);
        return make_completed(result);
    } catch (const std::exception &ex) {
        return make_failure(ex.what());
    }
}

}  // namespace

struct Translator::Impl {
    TranslationProfile profile;
    std::unique_ptr<ITranslationRuntime> runtime;
    std::unique_ptr<ITranslationRuntimeFactory> factory;
    ResolvedBackendEnvironment backend_environment;
    BackendOptions backend_options;
    TranslatorOptions fallback_options;
};

Translator::Translator(TranslatorOptions options)
    : impl_(std::make_unique<Impl>()) {
    impl_->fallback_options = std::move(options);
}

Translator::Translator(ResolvedBackendEnvironment environment,
                       BackendOptions backend_options,
                       TranslatorOptions options)
    : impl_(std::make_unique<Impl>()) {
    impl_->backend_environment = std::move(environment);
    impl_->backend_options = std::move(backend_options);
    impl_->fallback_options = std::move(options);
}

Translator::Translator(std::unique_ptr<ITranslationRuntimeFactory> factory,
                       TranslatorOptions options)
    : impl_(std::make_unique<Impl>()) {
    impl_->factory = std::move(factory);
    impl_->fallback_options = std::move(options);
}

Translator::Translator(std::unique_ptr<ITranslationRuntime> runtime, TranslatorOptions options)
    : impl_(std::make_unique<Impl>()) {
    impl_->runtime = std::move(runtime);
    impl_->fallback_options = std::move(options);
}

Translator::~Translator() = default;
Translator::Translator(Translator &&) noexcept = default;
Translator &Translator::operator=(Translator &&) noexcept = default;

std::string Translator::backend_label() const {
    if (impl_->runtime) return impl_->runtime->backend_label();
    if (impl_->backend_environment.initialized) return impl_->backend_environment.label;
    return "uninitialized";
}

void Translator::load(TranslationProfile profile) {
    if (auto err = profile.validate()) {
        throw std::invalid_argument(std::move(*err));
    }

    if (!impl_->runtime && std::holds_alternative<LocalModelConfig>(profile.model)) {
        const ResolvedBackendEnvironment &environment = impl_->backend_environment;
        if (!environment.initialized) {
            throw std::runtime_error("backend environment is not initialized");
        }
        if (!backend_request_available(impl_->backend_options, environment)) {
            throw std::runtime_error(unavailable_backend_message(impl_->backend_options));
        }
    }

    if (impl_->runtime) {
        impl_->runtime->unload();
    } else {
        if (!impl_->factory) {
            impl_->factory = std::make_unique<DefaultRuntimeFactory>(impl_->backend_environment,
                                                                     impl_->backend_options);
        }
        impl_->runtime = impl_->factory->create_runtime(profile.model);
    }

    impl_->runtime->load(profile.model, profile.options);
    impl_->profile = std::move(profile);
}

void Translator::unload_model() {
    if (impl_->runtime) impl_->runtime->unload();
    impl_->runtime.reset();
    impl_->profile = TranslationProfile{};
}

bool Translator::is_loaded() const {
    return impl_->runtime != nullptr && impl_->runtime->is_loaded();
}

RuntimeKind Translator::runtime_kind() const {
    return impl_->runtime ? impl_->runtime->kind() : RuntimeKind::Local;
}

int Translator::count_prompt_tokens(const std::string &text,
                                    const std::string &target_language) const {
    if (!impl_->runtime || !impl_->runtime->is_loaded() || !impl_->profile.prompt_strategy)
        return 0;
    const std::string prompt =
        impl_->profile.prompt_strategy->format_inference_prompt(text, target_language);
    return impl_->runtime->count_prompt_tokens(prompt);
}

TranslationResult Translator::translate(
    const TranslationRequest &request,
    const TranslationCallbacks &callbacks,
    const CancellationToken *cancel) {
    return translate(request, callbacks, cancel_checker(cancel));
}

TranslationResult Translator::translate(
    const TranslationRequest &request,
    const TranslationCallbacks &callbacks,
    std::function<bool()> should_cancel) {
    if (!impl_->runtime || !impl_->runtime->is_loaded() || !impl_->profile.prompt_strategy)
        return make_failure("model is not loaded");

    if (should_cancel && should_cancel()) return make_cancelled();

    const ITranslationPromptStrategy &prompt_strategy = *impl_->profile.prompt_strategy;
    const TranslatorOptions &options = impl_->profile.options;
    const RuntimeTraits traits = impl_->runtime->traits();

    auto do_step = [&](const std::string &text,
                       const std::string &target_language,
                       bool wordselect,
                       const std::function<void(const std::string &)> &on_token) -> TranslationResult {
        if (traits.context_handling == ContextHandling::LocalEnforced) {
            const int prompt_tokens =
                impl_->runtime->count_prompt_tokens(prompt_strategy.format_inference_prompt(
                    text, target_language));
            if (prompt_tokens <= 0) return make_failure("failed to measure prompt tokens");

            if (prompt_fits_context(prompt_tokens, options)) {
                return translate_single_chunk(*impl_->runtime, prompt_strategy, text,
                                              target_language, on_token, should_cancel);
            }

            if (wordselect) return make_failure(context_limit_error());

            const int max_chunk_tokens = max_chunk_prompt_tokens(options);
            const auto token_counter = [&](const std::string &segment) {
                return impl_->runtime->count_prompt_tokens(
                    prompt_strategy.format_inference_prompt(segment, target_language));
            };

            std::vector<std::string> chunks;
            try {
                chunks = chunk_by_token_budget(text, max_chunk_tokens, token_counter);
            } catch (const std::exception &ex) {
                return make_failure(ex.what());
            }

            if (chunks.empty()) return make_failure("failed to split text for translation");
            if (chunks.size() == 1) {
                return translate_single_chunk(*impl_->runtime, prompt_strategy, chunks.front(),
                                              target_language, on_token, should_cancel);
            }

            diagnostics::emit(DiagnosticLevel::Debug, "translator",
                              "translating in " + std::to_string(chunks.size()) + " chunks");

            std::string combined;
            for (size_t i = 0; i < chunks.size(); ++i) {
                if (should_cancel && should_cancel()) return make_cancelled();
                if (callbacks.on_chunk_begin) {
                    callbacks.on_chunk_begin(static_cast<int>(i + 1), static_cast<int>(chunks.size()));
                }

                auto chunk_result = translate_single_chunk(*impl_->runtime, prompt_strategy,
                                                           chunks[i], target_language, on_token,
                                                           should_cancel);
                if (chunk_result.outcome != TranslationOutcome::Completed) {
                    if (chunk_result.outcome == TranslationOutcome::Failed) {
                        chunk_result.error_message =
                            "translation failed at chunk " + std::to_string(i + 1) + "/" +
                            std::to_string(chunks.size()) + ": " + chunk_result.error_message;
                    }
                    return chunk_result;
                }
                combined += chunk_result.text;
            }

            return make_completed(std::move(combined));
        }

        return translate_single_chunk(*impl_->runtime, prompt_strategy, text, target_language,
                                      on_token, should_cancel);
    };

    if (callbacks.on_reset) callbacks.on_reset(false);
    auto forward = do_step(request.source, request.target_language, request.wordselect,
                           [&](const std::string &piece) {
                               if (callbacks.on_token) callbacks.on_token(piece);
                           });
    if (forward.outcome != TranslationOutcome::Completed) return forward;

    if (!request.back_translate) return forward;

    if (forward.text.empty() || request.source_language.empty()) {
        return make_failure("back-translate requires a non-empty forward result");
    }

    if (should_cancel && should_cancel()) return make_cancelled();
    if (callbacks.on_reset) callbacks.on_reset(true);
    return do_step(forward.text, request.source_language, false,
                   [&](const std::string &piece) {
                       if (callbacks.on_token) callbacks.on_token(piece);
                   });
}

}  // namespace qtrans::core
