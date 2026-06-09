#include "model/runtime_capabilities.h"

#include "ggml-backend.h"

namespace {

bool probe_vulkan_gpu() {
#ifdef QTRANS_MULTI_BACKEND
    const ggml_backend_reg_t reg = ggml_backend_reg_by_name("VULKAN");
    if (reg == nullptr) {
        return false;
    }

    const size_t device_count = ggml_backend_reg_dev_count(reg);
    for (size_t i = 0; i < device_count; ++i) {
        const ggml_backend_dev_t device = ggml_backend_reg_dev_get(reg, i);
        if (ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_GPU) {
            return true;
        }
    }

    const ggml_backend_dev_t gpu = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU);
    return gpu != nullptr;
#else
    return false;
#endif
}

}  // namespace

RuntimeCapabilities &RuntimeCapabilities::instance() {
    static RuntimeCapabilities caps;
    return caps;
}

void RuntimeCapabilities::set_plugin_dir(std::filesystem::path plugin_dir) {
    plugin_dir_ = std::move(plugin_dir);
}

void RuntimeCapabilities::refresh() {
    cpu_stq1_0_ = true;
    cpu_ggml_ = true;
    gpu_vulkan_ = probe_vulkan_gpu();
    refreshed_ = true;
}

bool RuntimeCapabilities::supports(InferenceBackend backend) const {
    switch (backend) {
        case InferenceBackend::CpuStq1_0:
            return cpu_stq1_0_;
        case InferenceBackend::CpuGgml:
            return cpu_ggml_;
        case InferenceBackend::GpuVulkan:
            return gpu_vulkan_;
    }
    return false;
}

std::string RuntimeCapabilities::describe(InferenceBackend backend) const {
    if (!supports(backend)) {
        return inference_backend_label(backend) + " (unavailable)";
    }
    return inference_backend_label(backend);
}

void RuntimeCapabilities::set_support(InferenceBackend backend, bool supported) {
    switch (backend) {
        case InferenceBackend::CpuStq1_0:
            cpu_stq1_0_ = supported;
            break;
        case InferenceBackend::CpuGgml:
            cpu_ggml_ = supported;
            break;
        case InferenceBackend::GpuVulkan:
            gpu_vulkan_ = supported;
            break;
    }
}

RuntimeCapabilities RuntimeCapabilitiesTestAccess::make_supported(
    std::initializer_list<InferenceBackend> backends) {
    RuntimeCapabilities caps;
    caps.cpu_stq1_0_ = false;
    caps.cpu_ggml_ = false;
    caps.gpu_vulkan_ = false;
    caps.refreshed_ = true;
    for (InferenceBackend backend : backends) {
        caps.set_support(backend, true);
    }
    return caps;
}
