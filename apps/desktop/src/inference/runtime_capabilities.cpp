#include "inference/runtime_capabilities.h"

#include "qtrans/core/backend_environment.h"

RuntimeCapabilities &RuntimeCapabilities::instance() {
    static RuntimeCapabilities caps;
    return caps;
}

void RuntimeCapabilities::set_plugin_dir(std::filesystem::path plugin_dir) {
    plugin_dir_ = std::move(plugin_dir);
}

void RuntimeCapabilities::refresh() {
    gpu_vulkan_ = qtrans::core::BackendEnvironment::has_vulkan();
    gpu_metal_ = qtrans::core::BackendEnvironment::has_metal();
    refreshed_ = true;
}

bool RuntimeCapabilities::supports(InferenceBackend backend) const {
    switch (backend) {
        case InferenceBackend::GpuVulkan:
            return gpu_vulkan_;
        case InferenceBackend::GpuMetal:
            return gpu_metal_;
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
        case InferenceBackend::GpuVulkan:
            gpu_vulkan_ = supported;
            break;
        case InferenceBackend::GpuMetal:
            gpu_metal_ = supported;
            break;
    }
}

RuntimeCapabilities RuntimeCapabilitiesTestAccess::make_supported(
    std::initializer_list<InferenceBackend> backends) {
    RuntimeCapabilities caps;
    caps.gpu_vulkan_ = false;
    caps.gpu_metal_ = false;
    caps.refreshed_ = true;
    for (InferenceBackend backend : backends) {
        caps.set_support(backend, true);
    }
    return caps;
}
