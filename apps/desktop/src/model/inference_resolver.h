#pragma once

#include "model/inference_backend.h"
#include "model/model_catalog.h"
#include "model/runtime_capabilities.h"

#include <optional>
#include <string>

struct TranslationModelConfig;

std::optional<ResolvedInference> resolve_inference(
    const ModelCatalogEntry &entry,
    const RuntimeCapabilities &caps);

std::string unavailable_reason(
    const ModelCatalogEntry &entry,
    const RuntimeCapabilities &caps);

TranslationModelConfig make_translation_config(const ResolvedInference &resolved);
