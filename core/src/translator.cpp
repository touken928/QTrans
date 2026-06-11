#include "qtrans/core/translator.h"

#include "runtime/local_runtime.h"
#include "runtime/remote_runtime.h"
#include "text/chunker.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <stdexcept>

namespace qtrans::core {

namespace {

TranslationResult make_failure(const std::string &message) {
    TranslationResult r;
    r.outcome = TranslationOutcome::Failed;
    r.error_message = message;
    return r;
}

TranslationResult make_cancelled() {
    TranslationResult r;
    r.outcome = TranslationOutcome::Cancelled;
    return r;
}

TranslationResult make_completed(std::string text) {
    TranslationResult r;
    r.outcome = TranslationOutcome::Completed;
    r.text = std::move(text);
    return r;
}

std::function<bool()> cancel_checker(const CancellationToken *cancel) {
    return cancel != nullptr ? cancel->checker() : std::function<bool()>();
}

int max_chunk_prompt_tokens(const TranslatorOptions &config) {
    const int output_room = std::max(kTranslationOutputReserve, config.n_ctx / 2);
    const int budget = config.n_ctx - output_room;
    return budget > 0 ? budget : 1;
}

bool prompt_fits_context(int prompt_tokens, const TranslatorOptions &config) {
    return prompt_tokens > 0 && prompt_tokens + kTranslationOutputReserve <= config.n_ctx;
}

std::string context_limit_error() {
    return "selected text exceeds the model context limit; select a shorter passage";
}

TranslationResult translate_single_chunk(
    ITranslationRuntime &runtime,
    const ITranslationModel &model,
    const std::string &text,
    const std::string &target_language,
    const std::function<void(const std::string &)> &on_token,
    const std::function<bool()> &should_cancel) {
    try {
        const std::string prompt = model.format_inference_prompt(text, target_language);
        const std::string result = runtime.translate(prompt, on_token, should_cancel);
        return make_completed(result);
    } catch (const std::exception &ex) {
        return make_failure(ex.what());
    }
}

}  // namespace

struct Translator::Impl {
    std::unique_ptr<ITranslationModel> model;
    std::unique_ptr<ITranslationRuntime> runtime;
    BackendOptions backend_options;
    TranslatorOptions fallback_options;
};

Translator::Translator(TranslatorOptions options)
    : impl_(std::make_unique<Impl>()) {
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

void Translator::initialize_backend(const BackendOptions &options) {
    impl_->backend_options = options;
}

std::string Translator::backend_label() const {
    if (impl_->runtime)
        return impl_->runtime->backend_label();
    return "uninitialized";
}

void Translator::load(std::unique_ptr<ITranslationModel> model) {
    if (model == nullptr) {
        throw std::invalid_argument("translation model is required");
    }

    unload_model();

    if (model->kind() == RuntimeKind::Local) {
        auto *local_model = dynamic_cast<ILocalTranslationModel *>(model.get());
        if (local_model == nullptr) {
            throw std::invalid_argument("local model does not implement ILocalTranslationModel");
        }

        auto local_rt = std::make_unique<LocalRuntime>();
        local_rt->initialize_backend(impl_->backend_options);
        local_rt->load_model(local_model->weights(), model->translator_options());
        impl_->runtime = std::move(local_rt);
    } else {
        auto *remote_model = dynamic_cast<IRemoteTranslationModel *>(model.get());
        if (remote_model == nullptr) {
            throw std::invalid_argument("remote model does not implement IRemoteTranslationModel");
        }

        auto remote_rt = std::make_unique<RemoteRuntime>();
        remote_rt->load_remote(remote_model->remote_config(), model->translator_options());
        impl_->runtime = std::move(remote_rt);
    }

    impl_->model = std::move(model);
}

void Translator::unload_model() {
    if (impl_->runtime)
        impl_->runtime->unload();
    impl_->runtime.reset();
    impl_->model.reset();
}

bool Translator::is_loaded() const {
    return impl_->runtime != nullptr && impl_->runtime->is_loaded();
}

RuntimeKind Translator::runtime_kind() const {
    return impl_->runtime ? impl_->runtime->kind() : RuntimeKind::Local;
}

int Translator::count_prompt_tokens(const std::string &text,
                                    const std::string &target_language) const {
    if (!impl_->runtime || !impl_->runtime->is_loaded() || impl_->model == nullptr)
        return 0;
    const std::string prompt = impl_->model->format_inference_prompt(text, target_language);
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
    if (!impl_->runtime || !impl_->runtime->is_loaded() || impl_->model == nullptr)
        return make_failure("model is not loaded");

    if (should_cancel && should_cancel())
        return make_cancelled();

    const ITranslationModel &model = *impl_->model;
    const TranslatorOptions &options = model.translator_options();
    auto logger = spdlog::get("inference");

    auto do_step = [&](const std::string &text,
                       const std::string &target_language,
                       bool wordselect,
                       const std::function<void(const std::string &)> &on_token) -> TranslationResult {
        const int prompt_tokens =
            impl_->runtime->count_prompt_tokens(model.format_inference_prompt(text, target_language));
        if (prompt_tokens <= 0)
            return make_failure("failed to measure prompt tokens");

        if (prompt_fits_context(prompt_tokens, options)) {
            return translate_single_chunk(*impl_->runtime, model, text, target_language, on_token,
                                          should_cancel);
        }

        if (wordselect)
            return make_failure(context_limit_error());

        const int max_chunk_tokens = max_chunk_prompt_tokens(options);
        const auto token_counter = [&](const std::string &segment) {
            return impl_->runtime->count_prompt_tokens(
                model.format_inference_prompt(segment, target_language));
        };

        std::vector<std::string> chunks;
        try {
            chunks = chunk_by_token_budget(text, max_chunk_tokens, token_counter);
        } catch (const std::exception &ex) {
            return make_failure(ex.what());
        }

        if (chunks.empty())
            return make_failure("failed to split text for translation");
        if (chunks.size() == 1)
            return translate_single_chunk(*impl_->runtime, model, chunks.front(), target_language,
                                          on_token, should_cancel);

        if (logger)
            logger->debug("translating in {} chunks", chunks.size());

        std::string combined;
        for (size_t i = 0; i < chunks.size(); ++i) {
            if (should_cancel && should_cancel()) return make_cancelled();
            if (callbacks.on_chunk_begin)
                callbacks.on_chunk_begin(static_cast<int>(i + 1), static_cast<int>(chunks.size()));

            auto chunk_result = translate_single_chunk(
                *impl_->runtime, model, chunks[i], target_language, on_token, should_cancel);

            if (chunk_result.outcome != TranslationOutcome::Completed) {
                if (chunk_result.outcome == TranslationOutcome::Failed)
                    chunk_result.error_message =
                        "translation failed at chunk " + std::to_string(i + 1) + "/" +
                        std::to_string(chunks.size()) + ": " + chunk_result.error_message;
                return chunk_result;
            }
            combined += chunk_result.text;
        }

        return make_completed(std::move(combined));
    };

    if (callbacks.on_reset) callbacks.on_reset(false);
    auto forward = do_step(request.source, request.target_language,
                           request.wordselect,
                           [&](const std::string &piece) {
                               if (callbacks.on_token) callbacks.on_token(piece);
                           });
    if (forward.outcome != TranslationOutcome::Completed)
        return forward;

    if (!request.back_translate)
        return forward;

    if (forward.text.empty() || request.source_language.empty())
        return make_failure("back-translate requires a non-empty forward result");

    if (should_cancel && should_cancel()) return make_cancelled();
    if (callbacks.on_reset) callbacks.on_reset(true);
    return do_step(forward.text, request.source_language, false,
                   [&](const std::string &piece) {
                       if (callbacks.on_token) callbacks.on_token(piece);
                   });
}

}  // namespace qtrans::core
