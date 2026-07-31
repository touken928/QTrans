#include "app/download_service.h"

#include <QMetaObject>
#include <QThread>

namespace {

DownloadState map_outcome(ExecutionOutcome outcome) {
    switch (outcome) {
        case ExecutionOutcome::Completed:
            return DownloadState::Completed;
        case ExecutionOutcome::Cancelled:
            return DownloadState::Cancelled;
        case ExecutionOutcome::Failed:
            return DownloadState::Failed;
    }
    return DownloadState::Failed;
}

}  // namespace

DownloadService::DownloadService(QObject *parent)
    : DownloadService(std::make_unique<ProductionModelDownloader>(), parent) {
}

DownloadService::DownloadService(std::unique_ptr<IModelDownloader> downloader,
                                 QObject *parent)
    : QObject(parent), download_executor_(std::move(downloader)) {
    qRegisterMetaType<DownloadId>("DownloadId");
    qRegisterMetaType<DownloadState>("DownloadState");
    qRegisterMetaType<DownloadResult>("DownloadResult");
}

DownloadService::~DownloadService() {
    if (download_thread_.joinable()) download_thread_.join();
}

void DownloadService::setDownloadRequest(const DownloadRequest &request) {
    std::lock_guard lock(mutex_);
    request_ = request;
}

DownloadId DownloadService::startDownload() {
    DownloadId id;
    {
        std::lock_guard lock(mutex_);
        id = DownloadId{next_download_id_++};
        DownloadRecord record;
        record.id = id;
        downloads_[id.value] = std::move(record);
    }
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, id] { beginDownload(id); }, Qt::QueuedConnection);
    } else {
        beginDownload(id);
    }
    return id;
}

void DownloadService::beginDownload(DownloadId id) {
    DownloadRequest request;
    std::shared_ptr<DownloadCancelToken> token;
    {
        std::lock_guard lock(mutex_);
        if (download_thread_.joinable()) download_thread_.join();
        request = request_;
        token = std::make_shared<DownloadCancelToken>();
        auto &record = downloads_[id.value];
        record.state = DownloadState::Running;
        record.cancel_token = token;
        if (record.cancel_requested) token->cancel();
    }
    emit downloadStarted(id);
    download_thread_ = std::thread([this, id, request = std::move(request), token] {
        const ExecutionResult result = download_executor_->download(
            request, token.get(), [this, id](const DownloadProgressData &progress) {
                QMetaObject::invokeMethod(this, [this, id, progress] { emit downloadProgress(id, progress.downloaded_bytes,
                                                                                             progress.total_bytes, progress.speed_bps,
                                                                                             progress.eta_seconds); }, Qt::QueuedConnection);
            });
        QMetaObject::invokeMethod(this, [this, id, result] { completeDownload(id, result); }, Qt::QueuedConnection);
    });
}

bool DownloadService::cancel(DownloadId id) {
    std::lock_guard lock(mutex_);
    const auto it = downloads_.find(id.value);
    if (it == downloads_.end()) return false;
    if (it->second.cancel_token) {
        it->second.cancel_token->cancel();
        return true;
    }
    it->second.cancel_requested = true;
    return true;
}

DownloadState DownloadService::downloadState(DownloadId id) const {
    std::lock_guard lock(mutex_);
    const auto it = downloads_.find(id.value);
    return it == downloads_.end() ? DownloadState::Failed : it->second.state;
}

void DownloadService::completeDownload(DownloadId id, const ExecutionResult &result) {
    const DownloadState state = map_outcome(result.outcome);
    {
        std::lock_guard lock(mutex_);
        downloads_[id.value].state = state;
    }
    DownloadResult typed;
    typed.id = id;
    typed.state = state;
    typed.error_message = result.error_message;
    emit downloadFinished(typed);
}

void DownloadService::shutdown() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, &DownloadService::shutdown,
                                  Qt::BlockingQueuedConnection);
        return;
    }
    {
        std::lock_guard lock(mutex_);
        for (auto &[id, record] : downloads_) {
            Q_UNUSED(id);
            if (record.cancel_token) record.cancel_token->cancel();
            record.cancel_requested = true;
        }
    }
    if (download_thread_.joinable()) download_thread_.join();
}
