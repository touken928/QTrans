#include "domain/download/model_downloader.h"

#include "domain/download/download.h"

#include <QCryptographicHash>
#include <QFile>

#include <filesystem>

bool download_file_matches_sha256(const std::string &path,
                                  const std::string &expected_sha256,
                                  std::string *actual_sha256) {
    QFile file(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    if (!file.open(QIODevice::ReadOnly)) return false;

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray block = file.read(1024 * 1024);
        if (block.isEmpty() && file.error() != QFileDevice::NoError) return false;
        hash.addData(block);
    }
    const std::string digest = hash.result().toHex().toStdString();
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

        download_set_quiet(true);
        download_set_progress_callback([on_progress = std::move(on_progress)](const DownloadProgress &progress) {
            if (on_progress) {
                on_progress({
                    progress.downloaded_bytes,
                    progress.total_bytes,
                    progress.speed_bytes_per_sec,
                    progress.eta_seconds,
                });
            }
        });
        download_to_file(request.local_path, spec, true, cancel_token);
        download_set_progress_callback(nullptr);
        std::string actual_digest;
        if (!download_file_matches_sha256(request.local_path,
                                          request.expected_sha256,
                                          &actual_digest)) {
            std::error_code remove_error;
            std::filesystem::remove(std::filesystem::u8path(request.local_path),
                                    remove_error);
            return {ExecutionOutcome::Failed,
                    "download checksum mismatch (expected " +
                        request.expected_sha256 + ", got " + actual_digest + ")"};
        }
        return {ExecutionOutcome::Completed, {}};
    } catch (const DownloadCancelled &) {
        download_set_progress_callback(nullptr);
        return {ExecutionOutcome::Cancelled, "download cancelled"};
    } catch (const std::exception &ex) {
        download_set_progress_callback(nullptr);
        return {ExecutionOutcome::Failed, ex.what()};
    }
}
