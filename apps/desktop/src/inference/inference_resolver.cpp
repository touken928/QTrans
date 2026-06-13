#include "inference/inference_resolver.h"

#include <sstream>

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

bool contains_backend(const std::vector<qtrans::core::BackendKind> &backends,
                      qtrans::core::BackendKind backend) {
    return std::find(backends.begin(), backends.end(), backend) != backends.end();
}

std::string diagnostic_summary(const ModelCatalogEntry &entry,
                               const RuntimeCapabilities &caps) {
    const auto &diagnostics = caps.environment().capabilities.diagnostics;
    std::ostringstream details;
    bool first = true;
    for (const auto &diagnostic : diagnostics) {
        if (!contains_backend(entry.backend_priority, diagnostic.backend)) {
            continue;
        }
        if (!first) {
            details << "; ";
        }
        first = false;
        details << backend_kind_label(diagnostic.backend) << ": " << diagnostic.message;
    }
    return details.str();
}

int n_gpu_layers_for(qtrans::core::BackendKind backend) {
    switch (backend) {
        case qtrans::core::BackendKind::Vulkan:
        case qtrans::core::BackendKind::Metal:
            return -1;
        case qtrans::core::BackendKind::Cpu:
            return 0;
    }
    return 0;
}

}  // namespace

std::optional<ResolvedInference> resolve_inference(
    const ModelCatalogEntry &entry,
    const RuntimeCapabilities &caps) {
    for (qtrans::core::BackendKind backend : entry.backend_priority) {
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
    for (qtrans::core::BackendKind backend : entry.backend_priority) {
        if (!first) {
            message << ", ";
        }
        first = false;
        message << caps.describe(backend);
    }
    const std::string details = diagnostic_summary(entry, caps);
    if (!details.empty()) {
        message << ". Details: " << details;
    }
    return message.str();
}
