#pragma once

#include "qtrans/core/backend_environment.h"
#include "qtrans/core/cancellation.h"
#include "qtrans/core/options.h"
#include "qtrans/core/translation_model.h"
#include "qtrans/core/translator.h"

#include "services/task/cancel_token.h"
#include "services/task/task_types.h"

#include <functional>
#include <memory>
#include <string>

class InferenceEngine {
public:
    InferenceEngine();
    ~InferenceEngine();

    InferenceEngine(const InferenceEngine &) = delete;
    InferenceEngine &operator=(const InferenceEngine &) = delete;
    InferenceEngine(InferenceEngine &&) noexcept;
    InferenceEngine &operator=(InferenceEngine &&) noexcept;

    void set_backend_context(const qtrans::core::ResolvedBackendEnvironment &environment,
                             const qtrans::core::BackendOptions &opts);
    void set_translator_options(const qtrans::core::TranslatorOptions &opts);

    bool is_loaded() const;
    std::string active_backend_label() const;

    void load(qtrans::core::TranslationProfile profile);
    void unload();

    TranslateStepResult translate(
        const std::string &text,
        const std::string &target_language,
        bool wordselect,
        const std::function<void(const std::string &)> &on_token,
        const CancelToken *cancel_token);

    TranslateStepResult run_translate_pipeline(
        const TranslatePipelinePayload &payload,
        const std::function<void(bool is_back_channel)> &on_reset,
        const std::function<void(bool is_back_channel, const std::string &piece)> &on_token,
        const CancelToken *cancel_token);

private:
    qtrans::core::TranslatorOptions options_{};
    qtrans::core::ResolvedBackendEnvironment backend_environment_{};
    qtrans::core::BackendOptions backend_options_{};
    std::unique_ptr<qtrans::core::Translator> translator_;
};
