#include "services/engine/inference_engine.h"

#include "log/component.h"
#include "log/logger.h"

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

void InferenceEngine::set_backend_options(const qtrans::core::BackendOptions &opts) {
    backend_options_ = opts;
}

void InferenceEngine::set_translator_options(const qtrans::core::TranslatorOptions &opts) {
    options_ = opts;
}

bool InferenceEngine::is_loaded() const {
    return translator_ != nullptr && translator_->is_loaded();
}

void InferenceEngine::load(qtrans::core::TranslationProfile profile) {
    if (profile.prompt_strategy == nullptr) {
        throw std::invalid_argument("translation prompt strategy is required");
    }

    options_ = profile.options;

    auto t = std::make_unique<qtrans::core::Translator>(options_);
    t->initialize_backend(backend_options_);
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

    if (on_reset) on_reset(false);

    const auto forward = translate(
        payload.source,
        payload.target_language,
        payload.wordselect,
        [&](const std::string &piece) {
            if (on_token) on_token(false, piece);
        },
        cancel_token);

    if (forward.outcome != InferenceOutcome::Completed) {
        return forward;
    }

    if (!payload.back_translate) {
        return forward;
    }

    if (forward.text.empty() || payload.source_language.empty()) {
        return make_failure("back-translate requires a non-empty forward result");
    }

    if (should_cancel && should_cancel()) {
        return make_cancelled();
    }

    if (on_reset) on_reset(true);

    return translate(
        forward.text,
        payload.source_language,
        false,
        [&](const std::string &piece) {
            if (on_token) on_token(true, piece);
        },
        cancel_token);
}
