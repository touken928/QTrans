#pragma once

#include "qtrans/core/translation_model.h"

#include <filesystem>

qtrans::core::TranslationProfile make_hymt2_7b_local_profile(
    const std::filesystem::path &path,
    int n_gpu_layers);
