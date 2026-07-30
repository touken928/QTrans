#pragma once

#include "qtrans/core.h"

#include "domain/tasks/cancel_token.h"
#include "domain/tasks/task_types.h"

#include <functional>
#include <memory>
#include <string>

struct InferenceEngineTestAccess;

class InferenceEngine {
public:
    InferenceEngine();
    ~InferenceEngine();

    InferenceEngine(const InferenceEngine &) = delete;
    InferenceEngine &operator=(const InferenceEngine &) = delete;
    InferenceEngine(InferenceEngine &&) noexcept;
    InferenceEngine &operator=(InferenceEngine &&) noexcept;

    void set_backend(qtrans::core::Backend backend);

    bool is_loaded() const;
    std::string active_backend_label() const;

    void load(qtrans::core::Model model);
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
    using TranslationDriver = std::function<qtrans::core::TranslationResult(
        const qtrans::core::TranslationRequest &,
        qtrans::core::TokenSink,
        qtrans::core::StopPredicate)>;

    explicit InferenceEngine(TranslationDriver driver);
    friend struct InferenceEngineTestAccess;

    qtrans::core::Backend backend_ = qtrans::core::Backend::Automatic;
    std::unique_ptr<qtrans::core::Translator> translator_;
    TranslationDriver driver_;
    bool driver_loaded_ = false;
};
