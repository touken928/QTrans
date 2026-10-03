#include "domain/batch/batch_output_writer.h"

#include "domain/batch/batch_file_handler.h"

#include <fstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

void remove_best_effort(const std::filesystem::path &path) {
    std::error_code error;
    std::filesystem::remove(path, error);
}

std::error_code commit_file(const std::filesystem::path &temporary,
                            const std::filesystem::path &destination) {
#ifdef _WIN32
    if (::MoveFileExW(temporary.c_str(), destination.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return {};
    }
    return {static_cast<int>(::GetLastError()), std::system_category()};
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    return error;
#endif
}

}  // namespace

BatchOutputWriteResult write_batch_output_atomic(
    const BatchEntry &entry, const std::filesystem::path &output_path) {
    BatchOutputWriteResult result;
    result.path = output_path;

    const auto *handler = get_handler(entry.file.file_type);
    if (!handler) {
        result.error_message = "unsupported batch output format";
        return result;
    }

    std::error_code error;
    std::filesystem::create_directories(output_path.parent_path(), error);
    if (error) {
        result.error_message = "cannot create output directory: " + error.message();
        return result;
    }

    auto temporary = output_path;
    temporary += ".tmp";
    const std::string content = handler->assembleOutput(entry.file);
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            result.error_message = "cannot create output file";
            return result;
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output.good()) {
            output.close();
            remove_best_effort(temporary);
            result.error_message = "cannot write output file";
            return result;
        }
        output.close();
        if (!output.good()) {
            remove_best_effort(temporary);
            result.error_message = "cannot close output file";
            return result;
        }
    }

    error = commit_file(temporary, output_path);
    if (error) {
        remove_best_effort(temporary);
        result.error_message = "cannot commit output file: " + error.message();
        return result;
    }

    result.success = true;
    return result;
}
