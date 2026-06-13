#pragma once

#include "catalog/model_catalog.h"
#include "inference/runtime_capabilities.h"

#include <optional>
#include <string>

#include "qtrans/core/options.h"

struct ResolvedInference {
    qtrans::core::BackendKind backend = qtrans::core::BackendKind::Vulkan;
    int n_gpu_layers = 0;
};

std::optional<ResolvedInference> resolve_inference(
    const ModelCatalogEntry &entry,
    const RuntimeCapabilities &caps);

std::string unavailable_reason(
    const ModelCatalogEntry &entry,
    const RuntimeCapabilities &caps);
