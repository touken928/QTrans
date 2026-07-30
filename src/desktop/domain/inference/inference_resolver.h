#pragma once

#include "domain/model-catalog/model_catalog.h"
#include "domain/inference/runtime_capabilities.h"

#include <optional>
#include <string>

#include "qtrans/core.h"

struct ResolvedInference {
    qtrans::core::Backend backend = qtrans::core::Backend::Vulkan;
};

std::optional<ResolvedInference> resolve_inference(
    const ModelCatalogEntry &entry,
    const RuntimeCapabilities &caps);

std::string unavailable_reason(
    const ModelCatalogEntry &entry,
    const RuntimeCapabilities &caps);
