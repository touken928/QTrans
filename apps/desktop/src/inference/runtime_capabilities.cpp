#include "inference/runtime_capabilities.h"

namespace {

std::string backend_kind_label(qtrans::core::BackendKind backend) {
    switch (backend) {
        case qtrans::core::BackendKind::Vulkan:
            return "Vulkan";
        case qtrans::core::BackendKind::Metal:
            return "Metal";
        case qtrans::core::BackendKind::Cpu:
        default:
            return "CPU";
    }
}

}  // namespace

RuntimeCapabilities &RuntimeCapabilities::instance() {
    static RuntimeCapabilities caps;
    return caps;
}

void RuntimeCapabilities::refresh(const qtrans::core::ResolvedBackendEnvironment &environment) {
    environment_ = environment;
    gpu_vulkan_ = environment.capabilities.vulkan_available;
    gpu_metal_ = environment.capabilities.metal_available;
    refreshed_ = true;
}

bool RuntimeCapabilities::supports(qtrans::core::BackendKind backend) const {
    if (!refreshed_) {
        return false;
    }
    switch (backend) {
        case qtrans::core::BackendKind::Vulkan:
            return gpu_vulkan_;
        case qtrans::core::BackendKind::Metal:
            return gpu_metal_;
        case qtrans::core::BackendKind::Cpu:
            return false;
    }
    return false;
}

std::string RuntimeCapabilities::describe(qtrans::core::BackendKind backend) const {
    if (!refreshed_) {
        return backend_kind_label(backend) + " (not initialized)";
    }
    if (!supports(backend)) {
        return backend_kind_label(backend) + " (unavailable)";
    }
    return backend_kind_label(backend);
}

const qtrans::core::ResolvedBackendEnvironment &RuntimeCapabilities::environment() const {
    return environment_;
}

void RuntimeCapabilities::set_support(qtrans::core::BackendKind backend, bool supported) {
    switch (backend) {
        case qtrans::core::BackendKind::Vulkan:
            gpu_vulkan_ = supported;
            break;
        case qtrans::core::BackendKind::Metal:
            gpu_metal_ = supported;
            break;
        case qtrans::core::BackendKind::Cpu:
            break;
    }
}

RuntimeCapabilities RuntimeCapabilitiesTestAccess::make_supported(
    std::initializer_list<qtrans::core::BackendKind> backends) {
    RuntimeCapabilities caps;
    caps.gpu_vulkan_ = false;
    caps.gpu_metal_ = false;
    caps.environment_ = qtrans::core::ResolvedBackendEnvironment{};
    caps.refreshed_ = true;
    for (qtrans::core::BackendKind backend : backends) {
        caps.set_support(backend, true);
    }
    caps.environment_.capabilities.vulkan_available = caps.gpu_vulkan_;
    caps.environment_.capabilities.metal_available = caps.gpu_metal_;
    if (caps.gpu_vulkan_) {
        caps.environment_.selected = qtrans::core::BackendKind::Vulkan;
        caps.environment_.label = "Vulkan";
    } else if (caps.gpu_metal_) {
        caps.environment_.selected = qtrans::core::BackendKind::Metal;
        caps.environment_.label = "Metal";
    }
    return caps;
}
