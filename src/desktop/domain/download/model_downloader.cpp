#include "domain/download/model_downloader.h"

#include "domain/download/download.h"

ExecutionResult ProductionModelDownloader::download(
    const DownloadModelPayload &payload,
    const CancelToken *cancel_token,
    DownloadProgressHandler on_progress) {
    try {
        DownloadSpec spec{};
        switch (payload.download_hub) {
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
        if (!download_parse_spec(payload.remote_spec, spec)) {
            throw std::runtime_error("invalid remote spec: " + payload.remote_spec);
        }
        if (!payload.modelscope_remote_spec.empty()) {
            DownloadSpec modelscope_spec{};
            if (download_parse_spec(payload.modelscope_remote_spec, modelscope_spec)) {
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
        download_to_file(payload.local_path, spec, true, cancel_token);
        download_set_progress_callback(nullptr);
        return {ExecutionOutcome::Completed, {}};
    } catch (const DownloadCancelled &) {
        download_set_progress_callback(nullptr);
        return {ExecutionOutcome::Cancelled, "download cancelled"};
    } catch (const std::exception &ex) {
        download_set_progress_callback(nullptr);
        return {ExecutionOutcome::Failed, ex.what()};
    }
}
