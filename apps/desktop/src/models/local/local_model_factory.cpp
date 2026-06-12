#include "models/local/local_model_factory.h"

#include "models/local/hymt2_18b_local_model.h"
#include "models/local/hymt2_7b_local_model.h"

#include <stdexcept>

qtrans::core::TranslationProfile create_local_model(
    const ModelCatalogEntry &entry,
    const std::filesystem::path &path,
    int n_gpu_layers) {
    if (entry.id == "hymt2-q4") {
        return make_hymt2_18b_local_profile(path, n_gpu_layers);
    }
    if (entry.id == "hymt2-7b-q4") {
        return make_hymt2_7b_local_profile(path, n_gpu_layers);
    }
    throw std::runtime_error("unsupported model id: " + entry.id);
}
