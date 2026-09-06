#pragma once

#include "qtrans/core.h"

#include <string>
#include <vector>

struct ModelCatalogEntry {
    std::string id;
    std::string display_name;
    std::string filename;
    std::string remote_spec;
    std::string modelscope_remote_spec;
    std::string sha256;
    int download_hub = 2;
    std::vector<qtrans::core::Backend> backend_priority;
};

const std::vector<ModelCatalogEntry> &model_catalog();

const ModelCatalogEntry *find_model_by_id(const std::string &id);

const ModelCatalogEntry *find_model_by_filename(const std::string &filename);

const ModelCatalogEntry *default_model();
