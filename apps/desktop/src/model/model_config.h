#pragma once

#include "model/inference_backend.h"

#include <cstdint>
#include <string>

struct TranslationModelConfig {
    int n_ctx = 4096;
    int max_tokens = 4096;
    float temperature = 0.7f;
    int top_k = 20;
    float top_p = 0.6f;
    float repeat_penalty = 1.05f;
    int n_gpu_layers = 0;
    InferenceBackend active_backend = InferenceBackend::GpuVulkan;
};
