#pragma once

#include "translation/translation_model.h"
#include "task/cancel_token.h"
#include "task/task_types.h"

#include <functional>
#include <memory>
#include <string>

class InferenceEngine {
public:
    bool is_loaded() const;

    void load(const std::string & model_path, const TranslationModelConfig & config);
    void unload();

    TranslateStepResult translate(
        const std::string & text,
        const std::string & target_language,
        const std::function<void(const std::string &)> & on_token,
        const CancelToken * cancel_token);

    TranslateStepResult run_translate_pipeline(
        const TranslatePipelinePayload & payload,
        const std::function<void(bool is_back_channel)> & on_reset,
        const std::function<void(bool is_back_channel, const std::string & piece)> & on_token,
        const CancelToken * cancel_token);

private:
    TranslationModelConfig config_{};
    std::unique_ptr<TranslationModel> model_;
};
