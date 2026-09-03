#include "domain/model-catalog/model_catalog.h"

#include <filesystem>

namespace {

const ModelCatalogEntry k_models[] = {
    {
        "hymt2-1.8b-q4",
        "Hy-MT2-1.8B (Q4)",
        "Hy-MT2-1.8B-Q4_K_M.gguf",
        "tencent/Hy-MT2-1.8B-GGUF/Hy-MT2-1.8B-Q4_K_M.gguf",
        "Tencent-Hunyuan/Hy-MT2-1.8B-GGUF/Hy-MT2-1.8B-Q4_K_M.gguf",
        "dc5f44fcf1fa496ee7ad725982c0c8c553a4de00259b53af84c4b89fb0c06699",
        2,
        {qtrans::core::Backend::Vulkan, qtrans::core::Backend::Metal},
    },
    {
        "hymt2-7b-q4",
        "Hy-MT2-7B (Q4)",
        "Hy-MT2-7B-Q4_K_M.gguf",
        "tencent/Hy-MT2-7B-GGUF/Hy-MT2-7B-Q4_K_M.gguf",
        "Tencent-Hunyuan/Hy-MT2-7B-GGUF/Hy-MT2-7B-Q4_K_M.gguf",
        "9f96256500f3fc1ab4d64336b58f52a949a95ad7516b0c229476eef782f9f77b",
        2,
        {qtrans::core::Backend::Vulkan, qtrans::core::Backend::Metal},
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
