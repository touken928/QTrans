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
InferenceEngine::InferenceEngine(TranslationDriver driver)
    : driver_(std::move(driver)),
      driver_loaded_(true) {
}
InferenceEngine::~InferenceEngine() = default;
InferenceEngine::InferenceEngine(InferenceEngine &&) noexcept = default;
InferenceEngine &InferenceEngine::operator=(InferenceEngine &&) noexcept = default;

void InferenceEngine::set_backend(qtrans::core::Backend backend) {
    backend_ = backend;
}

bool InferenceEngine::is_loaded() const {
    return driver_ ? driver_loaded_ : translator_ != nullptr && translator_->loaded();
}

std::string InferenceEngine::active_backend_label() const {
    if (translator_ != nullptr) {
        switch (translator_->backend()) {
            case qtrans::core::Backend::Vulkan:
                return "Vulkan";
            case qtrans::core::Backend::Metal:
                return "Metal";
            case qtrans::core::Backend::Cpu:
                return "CPU";
            case qtrans::core::Backend::Automatic:
            default:
                return "Automatic";
        }
    }
    switch (backend_) {
        case qtrans::core::Backend::Vulkan:
            return "Vulkan";
        case qtrans::core::Backend::Metal:
            return "Metal";
        case qtrans::core::Backend::Cpu:
            return "CPU";
        case qtrans::core::Backend::Automatic:
        default:
            return "Automatic";
    }
}

void InferenceEngine::load(qtrans::core::Model model) {
    if (driver_) {
        driver_loaded_ = true;
        return;
    }
    auto t = std::make_unique<qtrans::core::Translator>(backend_);
    t->load(std::move(model));

    translator_ = std::move(t);
}

void InferenceEngine::unload() {
    if (driver_) {
        driver_loaded_ = false;
        return;
    }
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
    req.text = text;
    req.target_language = target_language;
    req.overflow = wordselect ? qtrans::core::OverflowPolicy::Reject
                              : qtrans::core::OverflowPolicy::Split;
    qtrans::core::TokenSink token_sink = [&](std::string_view piece) {
        if (on_token) on_token(std::string(piece));
    };
    qtrans::core::StopPredicate should_cancel;
    if (cancel_token != nullptr) should_cancel = cancel_token->checker();
    const auto result = driver_ ? driver_(req, token_sink, should_cancel)
                                : translator_->translate(req, token_sink, should_cancel);
    switch (result.outcome) {
        case qtrans::core::TranslationOutcome::Completed:
            return make_completed(result.text);
        case qtrans::core::TranslationOutcome::Cancelled:
            return make_cancelled();
        default:
            return make_failure(result.message);
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

    qtrans::core::StopPredicate should_cancel;
    if (cancel_token != nullptr) should_cancel = cancel_token->checker();

    qtrans::core::TranslationRequest request;
    request.text = payload.source;
    request.target_language = payload.target_language;
    bool is_back_channel = false;
    if (cancel_token != nullptr && cancel_token->is_cancelled()) return make_cancelled();
    if (on_reset) on_reset(false);
    qtrans::core::TokenSink token_sink = [&](std::string_view piece) {
        if (on_token) on_token(is_back_channel, std::string(piece));
    };
    request.overflow = payload.wordselect ? qtrans::core::OverflowPolicy::Reject
                                          : qtrans::core::OverflowPolicy::Split;
    auto result = driver_ ? driver_(request, token_sink, should_cancel)
                          : translator_->translate(request, token_sink, should_cancel);
    if (result.outcome == qtrans::core::TranslationOutcome::Completed && payload.back_translate) {
        if (result.text.empty() || payload.source_language.empty()) {
            return make_failure("back-translate requires a non-empty forward result");
        }
        if (cancel_token != nullptr && cancel_token->is_cancelled()) return make_cancelled();
        is_back_channel = true;
        if (on_reset) on_reset(true);
        request.text = result.text;
        request.target_language = payload.source_language;
        request.overflow = qtrans::core::OverflowPolicy::Split;
        result = driver_ ? driver_(request, token_sink, should_cancel)
                         : translator_->translate(request, token_sink, should_cancel);
    }
    switch (result.outcome) {
        case qtrans::core::TranslationOutcome::Completed:
            return make_completed(result.text);
        case qtrans::core::TranslationOutcome::Cancelled:
            return make_cancelled();
        default:
            return make_failure(result.message);
    }
}
