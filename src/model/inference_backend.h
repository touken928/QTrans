#pragma once

#include <string>

enum class InferenceBackend {
    CpuStq1_0,
    CpuGgml,
    GpuVulkan,
};

struct ResolvedInference {
    InferenceBackend backend = InferenceBackend::CpuGgml;
    int n_gpu_layers = 0;
};

std::string inference_backend_label(InferenceBackend backend);
