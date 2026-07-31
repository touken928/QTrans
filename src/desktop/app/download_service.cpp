#include "app/download_service.h"

#include <QMetaObject>
#include <QThread>

#include <exception>
#include <string>
#include <utility>

namespace {

constexpr const char *kDownloadInProgressMessage =
    "another download is already in progress";
constexpr const char *kDownloadSupersededMessage =
    "download superseded before it started";

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
    bool rejected = false;
    std::uint64_t reservation_generation = 0;
    {
        std::lock_guard lock(mutex_);
        id = DownloadId{next_download_id_++};
        DownloadRecord record;
        record.id = id;
        // Snapshot the accepted request atomically with the reservation so a
        // later setDownloadRequest() cannot change this download's work.
        record.request = request_;
        if (active_) {
            // At most one download runs at a time; reject the new request
            // instead of blocking the owning thread on the active worker.
            record.state = DownloadState::Failed;
            rejected = true;
        } else {
            active_ = true;
            // The reservation binds to the current lifecycle generation; a
            // queued start for it is only valid while that generation holds.
            reservation_generation = generation_;
        }
        downloads_[id.value] = std::move(record);
    }
    if (QThread::currentThread() != thread()) {
        if (rejected) {
            QMetaObject::invokeMethod(this, [this, id] { rejectDownload(id, kDownloadInProgressMessage); }, Qt::QueuedConnection);
        } else {
            QMetaObject::invokeMethod(this, [this, id, reservation_generation] { beginDownload(id, reservation_generation); }, Qt::QueuedConnection);
        }
    } else if (rejected) {
        rejectDownload(id, kDownloadInProgressMessage);
    } else {
        beginDownload(id, reservation_generation);
    }
    return id;
}

void DownloadService::beginDownload(DownloadId id, std::uint64_t reservation_generation) {
    DownloadRequest request;
    std::shared_ptr<DownloadCancelToken> token;
    std::uint64_t worker_generation = 0;
    bool superseded = false;
    {
        std::lock_guard lock(mutex_);
        if (reservation_generation != generation_) {
            // A shutdown (or a later lifecycle) superseded this reservation
            // while its start was queued. Never spawn a worker and never touch
            // the current download_thread_ or active flag; resolve the
            // reservation as Failed instead.
            superseded = true;
        } else {
            auto &record = downloads_[id.value];
            request = record.request;
            token = std::make_shared<DownloadCancelToken>();
            record.state = DownloadState::Running;
            record.cancel_token = token;
            if (record.cancel_requested) token->cancel();
            worker_generation = ++generation_;
        }
    }
    if (superseded) {
        rejectDownload(id, kDownloadSupersededMessage);
        return;
    }
    // Spawn the worker BEFORE emitting downloadStarted. A directly-connected
    // downloadStarted slot can synchronously call shutdown() or startDownload()
    // while this call is still on the stack; by then the worker must already
    // exist so a reentrant shutdown joins it (no orphan worker) and a
    // reentrant start can never overwrite a joinable download_thread_ or
    // terminate the process.
    try {
        download_thread_ = std::thread([this, id, worker_generation, request = std::move(request), token] {
            ExecutionResult result;
            try {
                result = download_executor_->download(
                    request, token.get(),
                    [this, id](const DownloadProgressData &progress) {
                        QMetaObject::invokeMethod(this, [this, id, progress] { emit downloadProgress(id, progress.downloaded_bytes,
                                                                                                     progress.total_bytes, progress.speed_bps,
                                                                                                     progress.eta_seconds); }, Qt::QueuedConnection);
                    });
            } catch (const std::exception &error) {
                result = ExecutionResult{
                    ExecutionOutcome::Failed,
                    std::string("download raised exception: ") + error.what()};
            } catch (...) {
                result = ExecutionResult{ExecutionOutcome::Failed,
                                         "download raised unknown exception"};
            }
            QMetaObject::invokeMethod(this, [this, id, worker_generation, result] { completeDownload(id, worker_generation, result); }, Qt::QueuedConnection);
        });
    } catch (const std::exception &error) {
        // Thread spawn failed; finish Failed so the active state clears.
        completeDownload(id, worker_generation,
                         ExecutionResult{ExecutionOutcome::Failed,
                                         std::string("failed to start download thread: ") +
                                             error.what()});
        return;
    }
    emit downloadStarted(id);
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

void DownloadService::completeDownload(DownloadId id, std::uint64_t worker_generation,
                                       const ExecutionResult &result) {
    const DownloadState state = map_outcome(result.outcome);
    {
        std::lock_guard lock(mutex_);
        const auto it = downloads_.find(id.value);
        if (it != downloads_.end()) it->second.state = state;
        if (worker_generation != generation_) {
            // Completion from a superseded lifecycle (the worker was shut down
            // and possibly replaced by a later one). Record the outcome so
            // downloadState() stays accurate, but never join the current
            // worker thread or clear the active flag.
            return;
        }
        // The worker posts this completion as its last action, so joining here
        // only reaps a finished thread and never blocks the owning thread on a
        // live download.
        if (download_thread_.joinable()) download_thread_.join();
        active_ = false;
    }
    DownloadResult typed;
    typed.id = id;
    typed.state = state;
    typed.error_message = result.error_message;
    emit downloadFinished(typed);
}

void DownloadService::rejectDownload(DownloadId id, std::string message) {
    {
        std::lock_guard lock(mutex_);
        const auto it = downloads_.find(id.value);
        if (it != downloads_.end()) it->second.state = DownloadState::Failed;
    }
    DownloadResult typed;
    typed.id = id;
    typed.state = DownloadState::Failed;
    typed.error_message = std::move(message);
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
        // Bump the lifecycle generation so any queued in-flight completion
        // from before this shutdown is recognized as stale and discarded
        // instead of acting on a subsequent lifecycle.
        ++generation_;
        for (auto &[id, record] : downloads_) {
            Q_UNUSED(id);
            if (record.cancel_token) record.cancel_token->cancel();
            record.cancel_requested = true;
        }
    }
    if (download_thread_.joinable()) download_thread_.join();
    {
        std::lock_guard lock(mutex_);
        active_ = false;
    }
}
