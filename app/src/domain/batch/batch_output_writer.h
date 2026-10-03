#pragma once

#include "domain/batch/batch_types.h"

#include <filesystem>
#include <string>

struct BatchOutputWriteResult {
    bool success = false;
    std::filesystem::path path;
    std::string error_message;
};

// Renders a batch document and commits it atomically beside the destination.
// A failed write never replaces an existing output file.
BatchOutputWriteResult write_batch_output_atomic(
    const BatchEntry &entry, const std::filesystem::path &output_path);
