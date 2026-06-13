#include "catalog/model_catalog.h"

#include <filesystem>

namespace {

const ModelCatalogEntry k_models[] = {
    {
        "hymt2-q4",
        "Hy-MT2-1.8B (Q4)",
        "Hy-MT2-1.8B-Q4_K_M.gguf",
        "tencent/Hy-MT2-1.8B-GGUF/Hy-MT2-1.8B-Q4_K_M.gguf",
        "Tencent-Hunyuan/Hy-MT2-1.8B-GGUF/Hy-MT2-1.8B-Q4_K_M.gguf",
        2,
        {qtrans::core::BackendKind::Vulkan, qtrans::core::BackendKind::Metal},
    },
    {
        "hymt2-7b-q4",
        "Hy-MT2-7B (Q4)",
        "Hy-MT2-7B-Q4_K_M.gguf",
        "tencent/Hy-MT2-7B-GGUF/Hy-MT2-7B-Q4_K_M.gguf",
        "Tencent-Hunyuan/Hy-MT2-7B-GGUF/Hy-MT2-7B-Q4_K_M.gguf",
        2,
        {qtrans::core::BackendKind::Vulkan, qtrans::core::BackendKind::Metal},
    },
};

}  // namespace

const std::vector<ModelCatalogEntry> &model_catalog() {
    static const std::vector<ModelCatalogEntry> entries(std::begin(k_models), std::end(k_models));
    return entries;
}

const ModelCatalogEntry *find_model_by_id(const std::string &id) {
    for (const ModelCatalogEntry &entry : model_catalog()) {
        if (entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

const ModelCatalogEntry *find_model_by_filename(const std::string &filename) {
    const std::string leaf = std::filesystem::path(filename).filename().string();
    for (const ModelCatalogEntry &entry : model_catalog()) {
        if (entry.filename == leaf) {
            return &entry;
        }
    }
    return nullptr;
}

const ModelCatalogEntry *default_model() {
    return &model_catalog().front();
}
