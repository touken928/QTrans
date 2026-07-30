#include "domain/model-adapters/local_model_factory.h"

#include "domain/model-adapters/hymt2_18b_local_model.h"
#include "domain/model-adapters/hymt2_7b_local_model.h"

#include <stdexcept>

qtrans::core::Model create_local_model(
    const ModelCatalogEntry &entry,
    const std::filesystem::path &path) {
    if (entry.id == "hymt2-q4") {
        return make_hymt2_18b_local_model(path);
    }
    if (entry.id == "hymt2-7b-q4") {
        return make_hymt2_7b_local_model(path);
    }
    throw std::runtime_error("unsupported model id: " + entry.id);
}
