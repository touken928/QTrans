#pragma once

#include <string>

enum class InferenceBackend {
    GpuVulkan,
    GpuMetal,
};

struct ResolvedInference {
    InferenceBackend backend = InferenceBackend::GpuVulkan;
    int n_gpu_layers = 0;
};

std::string inference_backend_label(InferenceBackend backend);
