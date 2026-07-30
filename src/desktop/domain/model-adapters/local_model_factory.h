#pragma once

#include "domain/model-catalog/model_catalog.h"

#include "qtrans/core.h"

#include <filesystem>

qtrans::core::Model create_local_model(
    const ModelCatalogEntry &entry,
    const std::filesystem::path &path);
