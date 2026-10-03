#include "domain/inference/runtime_capabilities.h"

namespace {

std::string backend_kind_label(qtrans::core::Backend backend) {
    switch (backend) {
        case qtrans::core::Backend::Vulkan:
            return "Vulkan";
        case qtrans::core::Backend::Metal:
            return "Metal";
        case qtrans::core::Backend::Cpu:
        default:
            return "CPU";
    }
}

}  // namespace

RuntimeCapabilities &RuntimeCapabilities::instance() {
    static RuntimeCapabilities caps;
    return caps;
}

void RuntimeCapabilities::refresh(const qtrans::core::BackendState &environment) {
    environment_ = environment;
    gpu_vulkan_ = environment.capabilities.vulkan_available;
    gpu_metal_ = environment.capabilities.metal_available;
    refreshed_ = true;
}

bool RuntimeCapabilities::supports(qtrans::core::Backend backend) const {
    if (!refreshed_) {
        return false;
    }
    switch (backend) {
        case qtrans::core::Backend::Vulkan:
            return gpu_vulkan_;
        case qtrans::core::Backend::Metal:
            return gpu_metal_;
        case qtrans::core::Backend::Cpu:
        case qtrans::core::Backend::Automatic:
            return false;
    }
    return false;
}

std::string RuntimeCapabilities::describe(qtrans::core::Backend backend) const {
    if (!refreshed_) {
        return backend_kind_label(backend) + " (not initialized)";
    }
    if (!supports(backend)) {
        return backend_kind_label(backend) + " (unavailable)";
    }
    return backend_kind_label(backend);
}

const qtrans::core::BackendState &RuntimeCapabilities::environment() const {
    return environment_;
}

void RuntimeCapabilities::set_support(qtrans::core::Backend backend, bool supported) {
    switch (backend) {
        case qtrans::core::Backend::Vulkan:
            gpu_vulkan_ = supported;
            break;
        case qtrans::core::Backend::Metal:
            gpu_metal_ = supported;
            break;
        case qtrans::core::Backend::Cpu:
        case qtrans::core::Backend::Automatic:
            break;
    }
}

RuntimeCapabilities RuntimeCapabilitiesTestAccess::make_supported(
    std::initializer_list<qtrans::core::Backend> backends) {
    RuntimeCapabilities caps;
    caps.gpu_vulkan_ = false;
    caps.gpu_metal_ = false;
    caps.environment_ = qtrans::core::BackendState{};
    caps.refreshed_ = true;
    for (qtrans::core::Backend backend : backends) {
        caps.set_support(backend, true);
    }
    caps.environment_.capabilities.vulkan_available = caps.gpu_vulkan_;
    caps.environment_.capabilities.metal_available = caps.gpu_metal_;
    if (caps.gpu_vulkan_) {
        caps.environment_.selected = qtrans::core::Backend::Vulkan;
        caps.environment_.label = "Vulkan";
    } else if (caps.gpu_metal_) {
        caps.environment_.selected = qtrans::core::Backend::Metal;
        caps.environment_.label = "Metal";
    }
    return caps;
}
