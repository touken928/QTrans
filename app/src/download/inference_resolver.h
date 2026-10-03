#pragma once

#include "download/model_catalog.h"
#include "download/runtime_capabilities.h"

#include <optional>
#include <string>

struct ResolvedInference {
    qtrans::core::Backend backend = qtrans::core::Backend::Vulkan;
};

std::optional<ResolvedInference> resolve_inference(
    const ModelCatalogEntry &entry,
    const RuntimeCapabilities &caps);

std::string unavailable_reason(
    const ModelCatalogEntry &entry,
    const RuntimeCapabilities &caps);
