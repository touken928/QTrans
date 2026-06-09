#include "model/inference_backend.h"

namespace {

const char *label_for(InferenceBackend backend) {
    switch (backend) {
        case InferenceBackend::CpuStq1_0:
            return "STQ1_0 · CPU";
        case InferenceBackend::CpuGgml:
            return "CPU";
        case InferenceBackend::GpuVulkan:
            return "Vulkan";
    }
    return "unknown";
}

}  // namespace

std::string inference_backend_label(InferenceBackend backend) {
    return label_for(backend);
}
