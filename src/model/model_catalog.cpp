#include "model/model_catalog.h"

#include <filesystem>

namespace {

const ModelCatalogEntry k_models[] = {
    {
        "hymt2-125bit",
        "Hy-MT2-1.8B (1.25Bit)",
        "Hy-MT2-1.8B-1.25Bit.gguf",
        "AngelSlim/Hy-MT2-1.8B-1.25Bit-GGUF/Hy-MT2-1.8B-1.25Bit.gguf",
        2,
    },
    {
        "hymt2-2bit",
        "Hy-MT2-1.8B (2Bit)",
        "Hy-MT2-1.8B-2Bit.gguf",
        "tencent/Hy-MT2-1.8B-2Bit-GGUF/Hy-MT2-1.8B-2Bit.gguf",
        2,
    },
};

} // namespace

const std::vector<ModelCatalogEntry> & model_catalog() {
    static const std::vector<ModelCatalogEntry> entries(std::begin(k_models), std::end(k_models));
    return entries;
}

const ModelCatalogEntry * find_model_by_id(const std::string & id) {
    for (const ModelCatalogEntry & entry : model_catalog()) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

const ModelCatalogEntry * find_model_by_filename(const std::string & filename) {
    const std::string leaf = std::filesystem::path(filename).filename().string();
    for (const ModelCatalogEntry & entry : model_catalog()) {
        if (entry.filename == leaf) {
            return &entry;
        }
    }
    return nullptr;
}

const ModelCatalogEntry * default_model() {
    return &model_catalog().front();
}
