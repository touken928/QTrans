#include "translation/inference_engine.h"

#include "model/model_files.h"
#include "network/download.h"
#include "translation/hymt.h"

#include <stdexcept>

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

bool is_cancelled(const CancelToken *cancel_token) {
    return cancel_token != nullptr && cancel_token->is_cancelled();
}

}  // namespace

bool InferenceEngine::is_loaded() const {
    return model_ != nullptr && model_->is_loaded();
}

void InferenceEngine::load(const std::string &model_path, const TranslationModelConfig &config) {
    if (!download_file_exists(model_path)) {
        throw std::runtime_error("model file not found: " + model_path);
    }

    const std::vector<std::uint8_t> data = read_file_bytes(model_path);

    auto model = std::make_unique<Hymt>();
    model->load(data, config);

    config_ = config;
    model_ = std::move(model);
}

void InferenceEngine::unload() {
    model_.reset();
}

TranslateStepResult InferenceEngine::translate(
    const std::string &text,
    const std::string &target_language,
    const std::function<void(const std::string &)> &on_token,
    const CancelToken *cancel_token) {
    if (!is_loaded()) {
        return make_failure("model is not loaded");
    }

    if (is_cancelled(cancel_token)) {
        return make_cancelled();
    }

    try {
        const std::string result = model_->translate(
            text,
            target_language,
            on_token,
            cancel_token != nullptr ? cancel_token->checker() : std::function<bool()>());

        if (is_cancelled(cancel_token)) {
            return make_cancelled();
        }

        return make_completed(result);
    } catch (const TranslationCancelled &) {
        return make_cancelled();
    } catch (const std::exception &ex) {
        return make_failure(ex.what());
    }
}

TranslateStepResult InferenceEngine::run_translate_pipeline(
    const TranslatePipelinePayload &payload,
    const std::function<void(bool is_back_channel)> &on_reset,
    const std::function<void(bool is_back_channel, const std::string &piece)> &on_token,
    const CancelToken *cancel_token) {
    if (on_reset) {
        on_reset(false);
    }

    TranslateStepResult forward = translate(
        payload.source,
        payload.target_language,
        [&](const std::string &piece) {
            if (on_token) {
                on_token(false, piece);
            }
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

    if (is_cancelled(cancel_token)) {
        return make_cancelled();
    }

    if (on_reset) {
        on_reset(true);
    }

    return translate(
        forward.text,
        payload.source_language,
        [&](const std::string &piece) {
            if (on_token) {
                on_token(true, piece);
            }
        },
        cancel_token);
}
