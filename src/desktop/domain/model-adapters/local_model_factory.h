#pragma once

#include "domain/model-catalog/model_catalog.h"

#include "qtrans/core/translation_model.h"

#include <filesystem>

qtrans::core::TranslationProfile create_local_model(
    const ModelCatalogEntry &entry,
    const std::filesystem::path &path,
    int n_gpu_layers);
