#include "model/model_files.h"

#include <fstream>
#include <stdexcept>

std::vector<std::uint8_t> read_file_bytes(const std::string &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open model file: " + path);
    }

    input.seekg(0, std::ios::end);
    const std::streamsize size = input.tellg();
    if (size <= 0) {
        throw std::runtime_error("model file is empty: " + path);
    }

    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char *>(data.data()), size)) {
        throw std::runtime_error("failed to read model file: " + path);
    }

    return data;
}

void resolve_model_path(ModelLoadRequest &request) {
    DownloadSpec remote_spec = request.remote_spec;
    bool has_remote_source = request.has_remote_source;

    if (!has_remote_source) {
        std::string local_name;
        if (download_parse_uri(request.model_path, remote_spec, local_name)) {
            request.model_path = local_name;
            has_remote_source = true;
        }
    }

    if (has_remote_source) {
        if (!request.remote_revision.empty()) {
            remote_spec.revision = request.remote_revision;
        }
        download_ensure(request.model_path, remote_spec, request.force_download);
        return;
    }

    if (!download_file_exists(request.model_path)) {
        throw std::runtime_error(
            "model file not found: " + request.model_path +
            "\nuse --hf/--ms/--auto REPO/FILE or -m hf://... / ms://... / auto://... to download");
    }
}
