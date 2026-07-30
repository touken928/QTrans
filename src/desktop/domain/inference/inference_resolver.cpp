#include "domain/inference/inference_resolver.h"

#include <sstream>

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

bool contains_backend(const std::vector<qtrans::core::Backend> &backends,
                      qtrans::core::Backend backend) {
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

}  // namespace

std::optional<ResolvedInference> resolve_inference(
    const ModelCatalogEntry &entry,
    const RuntimeCapabilities &caps) {
    for (qtrans::core::Backend backend : entry.backend_priority) {
        if (!caps.supports(backend)) {
            continue;
        }

        ResolvedInference resolved{};
        resolved.backend = backend;
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
    for (qtrans::core::Backend backend : entry.backend_priority) {
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
