#include "qtrans/core.h"

#include "local_runtime.h"
#include "translation_control.h"
#include "text/chunker.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace qtrans::core {

namespace {

TranslationResult failure(TranslationErrorCode code, std::string message) {
    TranslationResult result;
    result.error_code = code;
    result.message = std::move(message);
    return result;
}

TranslationResult cancelled() {
    return {TranslationOutcome::Cancelled, TranslationErrorCode::Cancelled, {}, {}};
}

TranslationResult completed(std::string text) {
    return {TranslationOutcome::Completed, TranslationErrorCode::None, std::move(text), {}};
}

bool fits_context(int prompt_tokens, int context_tokens) {
    return prompt_tokens > 0 && prompt_tokens + kTranslationOutputReserve <= context_tokens;
}

int chunk_budget(int context_tokens) {
    const int output_room = std::max(kTranslationOutputReserve, context_tokens / 2);
    return std::max(1, context_tokens - output_room);
}

}  // namespace

struct Translator::Impl {
    Backend selected = Backend::Cpu;
    std::unique_ptr<LocalRuntime> runtime;
    Model model;
};

Translator::Translator(Backend backend)
    : impl_(std::make_unique<Impl>()) {
    const BackendState state = initialize_backend(backend);
    impl_->selected = state.selected;
    impl_->runtime = std::make_unique<LocalRuntime>(state);
}

Translator::~Translator() = default;
Translator::Translator(Translator &&) noexcept = default;
Translator &Translator::operator=(Translator &&) noexcept = default;

void Translator::load(Model model) {
    if (model.path.empty()) throw std::invalid_argument("model path is empty");
    if (!model.prompt_formatter) throw std::invalid_argument("prompt formatter is required");
    if (model.generation.context_tokens <= 0 || model.generation.max_output_tokens <= 0) {
        throw std::invalid_argument("generation token limits must be positive");
    }
    impl_->runtime->load(model);
    impl_->model = std::move(model);
}

void Translator::unload() noexcept {
    impl_->runtime->unload();
    impl_->model = {};
}

bool Translator::loaded() const noexcept {
    return impl_->runtime->loaded();
}

Backend Translator::backend() const noexcept {
    return impl_->selected;
}

TranslationResult Translator::translate(const TranslationRequest &request,
                                        TokenSink token_sink,
                                        StopPredicate stop) {
    if (!loaded()) return failure(TranslationErrorCode::NotLoaded, "model is not loaded");
    if (request.text.empty() || request.target_language.empty()) {
        return failure(TranslationErrorCode::InvalidRequest, "translation request is incomplete");
    }

    auto check_stop = [&]() -> TranslationResult {
        runtime_detail::CallbackState state;
        state.checker = stop;
        try {
            runtime_detail::check_stop(state);
        } catch (const runtime_detail::TranslationCancelled &) {
            return cancelled();
        } catch (const std::exception &ex) {
            return failure(TranslationErrorCode::Runtime, ex.what());
        } catch (...) {
            return failure(TranslationErrorCode::Runtime, "stop predicate failed");
        }
        return completed({});
    };

    const TranslationResult initial_stop = check_stop();
    if (initial_stop.outcome != TranslationOutcome::Completed) return initial_stop;

    auto translate_chunk = [&](const std::string &text,
                               const std::string &language) -> TranslationResult {
        std::string prompt;
        try {
            prompt = impl_->model.prompt_formatter(text, language);
        } catch (const std::exception &ex) {
            return failure(TranslationErrorCode::Runtime, ex.what());
        } catch (...) {
            return failure(TranslationErrorCode::Runtime, "prompt formatter failed");
        }
        try {
            const std::string output = impl_->runtime->translate(prompt, token_sink, stop);
            return completed(output);
        } catch (const runtime_detail::TranslationCancelled &) {
            return cancelled();
        } catch (const runtime_detail::TranslationCallbackFailed &ex) {
            return failure(TranslationErrorCode::Callback, ex.what());
        } catch (const std::exception &ex) {
            return failure(TranslationErrorCode::Runtime, ex.what());
        } catch (...) {
            return failure(TranslationErrorCode::Runtime, "translation failed");
        }
    };

    auto prompt_tokens_for = [&](const std::string &text) {
        return impl_->runtime->count_prompt_tokens(impl_->model.prompt_formatter(text, request.target_language));
    };

    int prompt_tokens = 0;
    try {
        prompt_tokens = prompt_tokens_for(request.text);
    } catch (const std::exception &ex) {
        return failure(TranslationErrorCode::Runtime, ex.what());
    } catch (...) {
        return failure(TranslationErrorCode::Runtime, "prompt formatter failed");
    }
    if (fits_context(prompt_tokens, impl_->model.generation.context_tokens)) {
        return translate_chunk(request.text, request.target_language);
    }
    if (request.overflow == OverflowPolicy::Reject) {
        return failure(TranslationErrorCode::ContextLimit,
                       "selected text exceeds the model context limit; select a shorter passage");
    }

    std::vector<std::string> chunks;
    try {
        chunks = chunk_by_token_budget(
            request.text, chunk_budget(impl_->model.generation.context_tokens),
            [&](const std::string &part) {
                return impl_->runtime->count_prompt_tokens(
                    impl_->model.prompt_formatter(part, request.target_language));
            });
    } catch (const std::exception &ex) {
        return failure(TranslationErrorCode::Runtime, ex.what());
    } catch (...) {
        return failure(TranslationErrorCode::Runtime, "failed to split translation text");
    }
    if (chunks.empty()) return failure(TranslationErrorCode::Runtime, "failed to split translation text");

    std::string combined;
    for (const std::string &chunk : chunks) {
        const TranslationResult stop_result = check_stop();
        if (stop_result.outcome != TranslationOutcome::Completed) return stop_result;
        const TranslationResult result = translate_chunk(chunk, request.target_language);
        if (result.outcome != TranslationOutcome::Completed) return result;
        combined += result.text;
    }
    return completed(std::move(combined));
}

}  // namespace qtrans::core
