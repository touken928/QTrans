#pragma once

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

class CancelToken;

struct DownloadCancelled : public std::runtime_error {
    DownloadCancelled()
        : std::runtime_error("download cancelled") {
    }
};

enum class ModelHub {
    HuggingFace,
    ModelScope,
    Auto,
};

struct DownloadSpec {
    ModelHub hub = ModelHub::Auto;
    std::string repo;
    std::string modelscope_repo;
    std::string filename;
    std::string revision;
};

bool download_parse_spec(const std::string &spec, DownloadSpec &out);

bool download_parse_uri(const std::string &uri, DownloadSpec &out, std::string &local_name);

std::string download_default_revision(ModelHub hub);

std::string download_resolve_url(const DownloadSpec &spec, ModelHub hub);

const char *download_hub_name(ModelHub hub);

bool download_file_exists(const std::string &path);

struct DownloadProgress {
    std::int64_t total_bytes = 0;
    std::int64_t downloaded_bytes = 0;
    double speed_bytes_per_sec = 0.0;
    double eta_seconds = -1.0;
};

using DownloadProgressCallback = std::function<void(const DownloadProgress &)>;

void download_set_progress_callback(DownloadProgressCallback callback);

ModelHub download_probe_hub(const DownloadSpec &spec);

void download_to_file(const std::string &local_path, const DownloadSpec &spec, bool force = false,
                      const CancelToken *cancel_token = nullptr);

void download_ensure(const std::string &local_path, const DownloadSpec &spec, bool force = false,
                     const CancelToken *cancel_token = nullptr);

void download_set_quiet(bool quiet);
