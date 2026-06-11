#pragma once

#include "network/download.h"
#include "translation/translation_model.h"

#include <string>
#include <vector>

struct ModelLoadRequest {
    std::string model_path;
    DownloadSpec remote_spec{};
    bool has_remote_source = false;
    std::string remote_revision;
    bool force_download = false;
    TranslationModelConfig config{};
};

std::vector<std::uint8_t> read_file_bytes(const std::string &path);

void resolve_model_path(ModelLoadRequest &request);
