#include "model/inference_backend.h"

namespace {

const char *label_for(InferenceBackend backend) {
    switch (backend) {
        case InferenceBackend::GpuVulkan:
            return "Vulkan";
        case InferenceBackend::GpuMetal:
            return "Metal";
    }
    return "unknown";
}

}  // namespace

std::string inference_backend_label(InferenceBackend backend) {
    return label_for(backend);
}
