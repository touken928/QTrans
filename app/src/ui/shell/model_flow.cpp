#include "ui/shell/model_flow.h"

#include "download/download_service.h"
#include "translate/inference_service.h"

ModelFlow::ModelFlow(InferenceService *inference_service,
                     DownloadService *download_service, QObject *parent)
    : QObject(parent),
      inference_service_(inference_service),
      download_service_(download_service) {
    connect(download_service_, &DownloadService::downloadStarted, this,
            &ModelFlow::onDownloadStarted);
    connect(download_service_, &DownloadService::downloadProgress, this,
            &ModelFlow::onDownloadProgress);
    connect(download_service_, &DownloadService::downloadFinished, this,
            &ModelFlow::onDownloadFinished);
    connect(inference_service_, &InferenceService::modelLoadFinished, this,
            &ModelFlow::onModelLoadFinished);
    connect(inference_service_, &InferenceService::modelUnloadFinished, this,
            &ModelFlow::onModelUnloadFinished);
}

void ModelFlow::beginDownloadThenLoad(const QString &model_id) {
    awaiting_download_load_ = true;
    // The visible modal belongs to the model flow: set before the emit so the
    // slot that creates the panel runs with the flag already true.
    model_flow_modal_active_ = true;
    emit showDownloadDialog();
    const DownloadId id = download_service_->startDownload();
    if (id.is_valid()) {
        bindActiveDownload(id, model_id);
        emit stateChanged();
    }
}

void ModelFlow::beginPageDownloadThenLoad(const QString &model_id) {
    awaiting_download_load_ = true;
    const DownloadId id = download_service_->startDownload();
    if (id.is_valid()) {
        bindActiveDownload(id, model_id);
        emit stateChanged();
    }
}

void ModelFlow::beginLoad(const QString &model_id) {
    loading_model_id_ = model_id;
    emit stateChanged();
    inference_service_->loadModel();
}

void ModelFlow::beginUnload() {
    unloading_ = true;
    emit stateChanged();
    inference_service_->unloadModel();
}

void ModelFlow::cancelFromDialog() {
    awaiting_download_load_ = false;
    noteModalHidden();
    emit hideModalRequested();
    if (active_download_id_.is_valid()) {
        download_service_->cancel(active_download_id_);
    }
}

void ModelFlow::cancelFromPage() {
    if (!active_download_id_.is_valid()) {
        return;
    }
    awaiting_download_load_ = false;
    noteModalHidden();
    emit hideModalRequested();
    download_service_->cancel(active_download_id_);
}

void ModelFlow::noteModalHidden() {
    model_flow_modal_active_ = false;
}

void ModelFlow::bindActiveDownload(DownloadId id, const QString &model_id) {
    active_download_id_ = id;
    active_download_model_id_ = model_id;
    download_active_ = true;
}

void ModelFlow::onDownloadStarted(DownloadId id) {
    // The reserved id was bound synchronously when the request was made, so
    // this event only confirms it. A mismatched id belongs to a stale or
    // superseded lifecycle and must never displace the correlated binding.
    if (!active_download_id_.is_valid() || id != active_download_id_) {
        return;
    }
    emit stateChanged();
}

void ModelFlow::onDownloadProgress(DownloadId id, qint64 downloaded, qint64 total,
                                   double speed_bps, double eta_seconds) {
    if (!active_download_id_.is_valid() || id != active_download_id_) {
        return;
    }
    last_download_done_ = downloaded;
    last_download_total_ = total;
    emit downloadProgress(downloaded, total, speed_bps, eta_seconds);
}

void ModelFlow::onDownloadFinished(const DownloadResult &result) {
    // Ignore completions from earlier/consecutive downloads; they must not
    // clear state or trigger a load.
    if (!active_download_id_.is_valid() || result.id != active_download_id_) {
        return;
    }
    // Capture the correlated model id before clearing the binding; the
    // follow-up load targets the model whose file this download wrote.
    const QString model_id = active_download_model_id_;
    download_active_ = false;
    active_download_id_ = DownloadId{};
    active_download_model_id_.clear();
    last_download_done_ = 0;
    last_download_total_ = 0;

    if (result.state != DownloadState::Completed) {
        awaiting_download_load_ = false;
        emit downloadFailed();
        emit stateChanged();
        return;
    }

    if (awaiting_download_load_) {
        awaiting_download_load_ = false;
        emit downloadReadyToLoad();
        beginLoad(model_id);
    }
    emit stateChanged();
}

void ModelFlow::onModelLoadFinished(bool success, const QString &error_message,
                                    const QString &backend_label) {
    loading_model_id_.clear();
    emit loadFinished(success, error_message, backend_label);
    // Emit before the success/fail branch so the shell projection sees the
    // cleared loading id even when a modal decision follows.
    emit stateChanged();

    if (success) {
        // Close only the modal this flow opened for the download panel.
        if (model_flow_modal_active_) {
            emit hideModalRequested();
        }
        return;
    }

    QString message = error_message.trimmed();
    if (message.isEmpty()) {
        message = QStringLiteral("Failed to load the model.");
    }
    emit alertRequested(QStringLiteral("Failed to Load Model"), message);
}

void ModelFlow::onModelUnloadFinished(bool success, const QString &error_message) {
    // The unload result is terminal in both directions: the busy state must
    // always be cleared so the shell never stays locked after a failed unload.
    unloading_ = false;
    emit unloadFinished(success, error_message);
    emit stateChanged();
}
