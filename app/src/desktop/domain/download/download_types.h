#pragma once

#include <cstdint>
#include <string>

// Typed desktop download domain contracts. Qt-free; DownloadService adapts
// them to the download executor and republishes typed progress/completion.

struct DownloadId {
    std::uint64_t value = 0;

    bool operator==(const DownloadId &other) const {
        return value == other.value;
    }
    bool operator!=(const DownloadId &other) const {
        return value != other.value;
    }
    bool is_valid() const {
        return value != 0;
    }
};

enum class DownloadState {
    Pending,
    Running,
    Completed,
    Failed,
    Cancelled,
};

struct DownloadRequest {
    std::string local_path;
    std::string remote_spec;
    std::string modelscope_remote_spec;
    // Lowercase SHA-256 pinned by the model catalog. Production downloads are
    // not accepted until the complete file matches this digest.
    std::string expected_sha256;
    int download_hub = 2;  // 0 = HuggingFace, 1 = ModelScope, otherwise Auto
};

struct DownloadResult {
    DownloadId id;
    DownloadState state = DownloadState::Pending;
    std::string error_message;
};
