#include "translation/inference_engine.h"

#include "model/model_files.h"
#include "network/download.h"
#include "translation/hymt.h"
#include "translation/text_chunker.h"

#include <algorithm>
#include <cstdio>
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

int max_chunk_prompt_tokens(const TranslationModelConfig &config) {
    // Translation output is often similar in length to the source; reserve ~half of
    // n_ctx for generation so a chunk does not fill the window with prompt alone.
    const int output_room = std::max(kTranslationOutputReserve, config.n_ctx / 2);
    const int budget = config.n_ctx - output_room;
    return budget > 0 ? budget : 1;
}

bool prompt_fits_context(int prompt_tokens, const TranslationModelConfig &config) {
    return prompt_tokens > 0 && prompt_tokens + kTranslationOutputReserve <= config.n_ctx;
}

std::string context_limit_error() {
    return "selected text exceeds the model context limit; select a shorter passage";
}

TranslateStepResult translate_single_chunk(
    Hymt *model,
    const std::string &text,
    const std::string &target_language,
    const std::function<void(const std::string &)> &on_token,
    const CancelToken *cancel_token) {
    if (is_cancelled(cancel_token)) {
        return make_cancelled();
    }

    try {
        const std::string result = model->translate(
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
    bool wordselect,
    const std::function<void(const std::string &)> &on_token,
    const CancelToken *cancel_token) {
    if (!is_loaded()) {
        return make_failure("model is not loaded");
    }

    if (is_cancelled(cancel_token)) {
        return make_cancelled();
    }

    Hymt *hymt = dynamic_cast<Hymt *>(model_.get());
    if (hymt == nullptr) {
        return make_failure("unsupported translation model");
    }

    const int prompt_tokens = hymt->count_prompt_tokens(text, target_language);
    if (prompt_tokens <= 0) {
        return make_failure("failed to measure prompt tokens");
    }

    if (prompt_fits_context(prompt_tokens, config_)) {
        return translate_single_chunk(hymt, text, target_language, on_token, cancel_token);
    }

    if (wordselect) {
        return make_failure(context_limit_error());
    }

    const int max_chunk_tokens = max_chunk_prompt_tokens(config_);
    const auto token_counter = [&](const std::string &segment) {
        return hymt->count_prompt_tokens(segment, target_language);
    };

    std::vector<std::string> chunks;
    try {
        chunks = chunk_text_by_token_budget(text, max_chunk_tokens, token_counter);
    } catch (const std::exception &ex) {
        return make_failure(ex.what());
    }

    if (chunks.empty()) {
        return make_failure("failed to split text for translation");
    }

    if (chunks.size() == 1) {
        return translate_single_chunk(hymt, chunks.front(), target_language, on_token, cancel_token);
    }

    std::string combined;
    for (size_t i = 0; i < chunks.size(); ++i) {
        if (is_cancelled(cancel_token)) {
            return make_cancelled();
        }

        fprintf(stderr,
                "[InferenceEngine] translating chunk %zu/%zu\n",
                i + 1,
                chunks.size());

        // Each chunk streams through the same on_token callback; no reset between chunks.
        TranslateStepResult chunk_result = translate_single_chunk(
            hymt,
            chunks[i],
            target_language,
            on_token,
            cancel_token);

        if (chunk_result.outcome != InferenceOutcome::Completed) {
            if (chunk_result.outcome == InferenceOutcome::Failed) {
                chunk_result.error_message = "translation failed at chunk " + std::to_string(i + 1) + "/" +
                                             std::to_string(chunks.size()) + ": " + chunk_result.error_message;
            }
            return chunk_result;
        }

        combined += chunk_result.text;
    }

    return make_completed(std::move(combined));
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
        payload.wordselect,
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
        false,
        [&](const std::string &piece) {
            if (on_token) {
                on_token(true, piece);
            }
        },
        cancel_token);
}
