#include "download/model_downloader.h"

#include "download/download.h"
#include "download/sha256.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

bool download_file_matches_sha256(const std::string &path,
                                  const std::string &expected_sha256,
                                  std::string *actual_sha256) {
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    if (!file.is_open()) return false;

    qtrans::download_detail::Sha256 hash;
    std::vector<char> buffer(1024 * 1024);
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read_count = file.gcount();
        if (read_count > 0) {
            hash.update(reinterpret_cast<const std::uint8_t *>(buffer.data()),
                        static_cast<std::size_t>(read_count));
        }
    }
    if (file.bad()) return false;

    const std::string digest = hash.hex_digest();
    if (actual_sha256 != nullptr) *actual_sha256 = digest;
    return digest == expected_sha256;
}

ExecutionResult ProductionModelDownloader::download(
    const DownloadRequest &request,
    const DownloadCancelToken *cancel_token,
    DownloadProgressHandler on_progress) {
    try {
        if (request.expected_sha256.empty()) {
            return {ExecutionOutcome::Failed,
                    "download rejected: model digest is not pinned"};
        }
        DownloadSpec spec{};
        switch (request.download_hub) {
            case 0:
                spec.hub = ModelHub::HuggingFace;
                break;
            case 1:
                spec.hub = ModelHub::ModelScope;
                break;
            default:
                spec.hub = ModelHub::Auto;
                break;
        }
        if (!download_parse_spec(request.remote_spec, spec)) {
            throw std::runtime_error("invalid remote spec: " + request.remote_spec);
        }
        if (!request.modelscope_remote_spec.empty()) {
            DownloadSpec modelscope_spec{};
            if (download_parse_spec(request.modelscope_remote_spec, modelscope_spec)) {
                spec.modelscope_repo = modelscope_spec.repo;
            }
        }

        DownloadProgressCallback progress =
            [on_progress = std::move(on_progress)](const DownloadProgress &download_progress) {
                if (on_progress) {
                    on_progress({
                        download_progress.downloaded_bytes,
                        download_progress.total_bytes,
                        download_progress.speed_bytes_per_sec,
                        download_progress.eta_seconds,
                    });
                }
            };
        // The digest is produced by hashing the bytes as they are written; the
        // just-downloaded file is never re-read here. A thrown transfer skips
        // the comparison below entirely, so a partial stream is never checked.
        const std::string actual_digest =
            download_to_file(request.local_path, spec, true, cancel_token, progress,
                             /*quiet=*/true);
        if (actual_digest != request.expected_sha256) {
            std::error_code remove_error;
            std::filesystem::remove(std::filesystem::u8path(request.local_path),
                                    remove_error);
            return {ExecutionOutcome::Failed,
                    "download checksum mismatch (expected " + request.expected_sha256 +
                        ", got " + actual_digest + ")"};
        }
        return {ExecutionOutcome::Completed, {}};
    } catch (const DownloadCancelled &) {
        return {ExecutionOutcome::Cancelled, "download cancelled"};
    } catch (const std::exception &ex) {
        return {ExecutionOutcome::Failed, ex.what()};
    }
}
