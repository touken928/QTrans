#include "model/inference_resolver.h"

#include "model/model_config.h"

#include <sstream>

namespace {

int n_gpu_layers_for(InferenceBackend backend) {
    switch (backend) {
        case InferenceBackend::GpuVulkan:
        case InferenceBackend::GpuMetal:
            return -1;
    }
    return 0;
}

}  // namespace

std::optional<ResolvedInference> resolve_inference(
    const ModelCatalogEntry &entry,
    const RuntimeCapabilities &caps) {
    for (InferenceBackend backend : entry.backend_priority) {
        if (!caps.supports(backend)) {
            continue;
        }

        ResolvedInference resolved{};
        resolved.backend = backend;
        resolved.n_gpu_layers = n_gpu_layers_for(backend);
        return resolved;
    }

    return std::nullopt;
}

std::string unavailable_reason(
    const ModelCatalogEntry &entry,
    const RuntimeCapabilities &caps) {
    std::ostringstream message;
    message << "No supported inference backend for model \"" << entry.display_name << "\". Required: ";
    bool first = true;
    for (InferenceBackend backend : entry.backend_priority) {
        if (!first) {
            message << ", ";
        }
        first = false;
        message << caps.describe(backend);
    }
    return message.str();
}

TranslationModelConfig make_translation_config(const ResolvedInference &resolved) {
    TranslationModelConfig config{};
    config.n_gpu_layers = resolved.n_gpu_layers;
    config.active_backend = resolved.backend;
    return config;
}
