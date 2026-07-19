#pragma once

#include "domain/inference/engine/inference_engine.h"
#include "domain/tasks/task_execution.h"

class ProductionTranslationSession final : public ITranslationSession {
public:
    void initialize_backend() override;
    std::string active_backend_label() const override;
    bool is_loaded() const override;
    ExecutionResult load(const LoadModelPayload &payload) override;
    ExecutionResult unload() override;
    ExecutionResult translate(
        const TranslatePipelinePayload &payload,
        TranslationResetHandler on_reset,
        TranslationTokenHandler on_token,
        const CancelToken *cancel_token) override;

private:
    InferenceEngine engine_;
};
