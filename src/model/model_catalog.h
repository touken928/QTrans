#pragma once

#include <string>
#include <vector>

struct ModelCatalogEntry {
    std::string id;
    std::string display_name;
    std::string filename;
    std::string remote_spec;
    int download_hub = 2;
};

const std::vector<ModelCatalogEntry> &model_catalog();

const ModelCatalogEntry *find_model_by_id(const std::string &id);

const ModelCatalogEntry *find_model_by_filename(const std::string &filename);

const ModelCatalogEntry *default_model();
