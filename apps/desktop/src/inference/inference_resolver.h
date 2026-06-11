#pragma once

#include "inference/inference_backend.h"
#include "catalog/model_catalog.h"
#include "inference/runtime_capabilities.h"

#include <optional>
#include <string>

#include "qtrans/core/options.h"

std::optional<ResolvedInference> resolve_inference(
    const ModelCatalogEntry &entry,
    const RuntimeCapabilities &caps);

std::string unavailable_reason(
    const ModelCatalogEntry &entry,
    const RuntimeCapabilities &caps);

qtrans::core::TranslatorOptions make_translator_options(const ResolvedInference &resolved);
