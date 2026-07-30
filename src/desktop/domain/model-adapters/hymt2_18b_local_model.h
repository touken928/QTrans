#pragma once

#include "qtrans/core.h"

#include <filesystem>

qtrans::core::Model make_hymt2_18b_local_model(
    const std::filesystem::path &path);
