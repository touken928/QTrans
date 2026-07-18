#include "domain/inference/engine/inference_engine.h"

#include "domain/logging/component.h"
#include "domain/logging/logger.h"

#include <stdexcept>
#include <utility>

namespace {

TranslateStepResult make_failure(const std::string &message) {
    TranslateStepResult result{};
    result.outcome = InferenceOutcome::Failed;
    result.error_message = message;
    return result;
}

TranslateStepResult make_cancelled() {
    TranslateStepResult result{};
    result.outcome = InferenceOutcome::Cancelled;
    return result;
}

TranslateStepResult make_completed(std::string text) {
    TranslateStepResult result{};
    result.outcome = InferenceOutcome::Completed;
    result.text = std::move(text);
    return result;
}

}  // namespace

InferenceEngine::InferenceEngine() = default;
InferenceEngine::~InferenceEngine() = default;
InferenceEngine::InferenceEngine(InferenceEngine &&) noexcept = default;
InferenceEngine &InferenceEngine::operator=(InferenceEngine &&) noexcept = default;

void InferenceEngine::set_backend_context(
    const qtrans::core::ResolvedBackendEnvironment &environment,
    const qtrans::core::BackendOptions &opts) {
    backend_environment_ = environment;
    backend_options_ = opts;
}

void InferenceEngine::set_translator_options(const qtrans::core::TranslatorOptions &opts) {
    options_ = opts;
}

bool InferenceEngine::is_loaded() const {
    return translator_ != nullptr && translator_->is_loaded();
}

std::string InferenceEngine::active_backend_label() const {
    if (translator_ != nullptr) {
        return translator_->backend_label();
    }
    return backend_environment_.label;
}

void InferenceEngine::load(qtrans::core::TranslationProfile profile) {
    if (profile.prompt_strategy == nullptr) {
        throw std::invalid_argument("translation prompt strategy is required");
    }

    options_ = profile.options;

    auto t = std::make_unique<qtrans::core::Translator>(backend_environment_, backend_options_, options_);
    t->load(std::move(profile));

    translator_ = std::move(t);
}

void InferenceEngine::unload() {
    translator_.reset();
}

TranslateStepResult InferenceEngine::translate(
    const std::string &text,
    const std::string &target_language,
    bool wordselect,
    const std::function<void(const std::string &)> &on_token,
    const CancelToken *cancel_token) {
    if (!is_loaded()) {
        return make_failure("model is not loaded");
    }

    qtrans::core::TranslationRequest req;
    req.source = text;
    req.target_language = target_language;
    req.wordselect = wordselect;
    req.back_translate = false;

    qtrans::core::TranslationCallbacks cb;
    cb.on_token = on_token;

    std::function<bool()> should_cancel;
    if (cancel_token != nullptr) {
        should_cancel = cancel_token->checker();
    }

    const auto result = translator_->translate(req, cb, should_cancel);
    switch (result.outcome) {
        case qtrans::core::TranslationOutcome::Completed:
            return make_completed(result.text);
        case qtrans::core::TranslationOutcome::Cancelled:
            return make_cancelled();
        default:
            return make_failure(result.error_message);
    }
}

TranslateStepResult InferenceEngine::run_translate_pipeline(
    const TranslatePipelinePayload &payload,
    const std::function<void(bool is_back_channel)> &on_reset,
    const std::function<void(bool is_back_channel, const std::string &piece)> &on_token,
    const CancelToken *cancel_token) {
    if (!is_loaded()) {
        return make_failure("model is not loaded");
    }

    std::function<bool()> should_cancel;
    if (cancel_token != nullptr) {
        should_cancel = cancel_token->checker();
    }

    qtrans::core::TranslationRequest request;
    request.source = payload.source;
    request.target_language = payload.target_language;
    request.source_language = payload.source_language;
    request.back_translate = payload.back_translate;
    request.wordselect = payload.wordselect;

    qtrans::core::TranslationCallbacks callbacks;
    bool is_back_channel = false;
    callbacks.on_reset = [&](bool reset_back_channel) {
        is_back_channel = reset_back_channel;
        if (on_reset) {
            on_reset(reset_back_channel);
        }
    };
    callbacks.on_token = [&](const std::string &piece) {
        if (on_token) {
            on_token(is_back_channel, piece);
        }
    };

    const auto result = translator_->translate(request, callbacks, should_cancel);
    switch (result.outcome) {
        case qtrans::core::TranslationOutcome::Completed:
            return make_completed(result.text);
        case qtrans::core::TranslationOutcome::Cancelled:
            return make_cancelled();
        default:
            return make_failure(result.error_message);
    }
}
