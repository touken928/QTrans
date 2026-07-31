#pragma once

#include "domain/download/download_types.h"
#include "domain/download/model_downloader.h"
#include "domain/download/download_cancellation.h"

#include <QMetaType>
#include <QObject>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

Q_DECLARE_METATYPE(DownloadId)
Q_DECLARE_METATYPE(DownloadState)
Q_DECLARE_METATYPE(DownloadResult)

// Sole owner of dedicated model download execution. Runs downloads on a
// dedicated std::thread with a shared DownloadCancelToken; progress and
// completion are reposted to this object and republished as typed signals
// carrying the exact source DownloadId. Has no ModelHost dependency;
// MainWindow coordinates download completion with inference load.
//
// Concurrency: at most one download is active at a time. startDownload()
// reserves the DownloadId synchronously from any thread and decides there
// whether the request can start; a request that arrives while another
// download is active is promptly finished Failed with a clear message instead
// of being queued behind it. The accepted DownloadRequest is snapshotted under
// the same mutex hold as the reservation, so a later setDownloadRequest()
// cannot alter the work an accepted download performs. The actual start
// (Running state, downloadStarted delivery, thread spawn) is queued onto this
// object's owning thread, and the download runs on a dedicated thread that
// never blocks the Qt event loop. The worker thread is joined only after it
// has completed; starting a download never joins or waits on the owning
// thread. Each reservation captures the lifecycle generation at startDownload()
// time and each spawned worker carries the generation it was started under: a
// queued beginDownload superseded by shutdown never starts a worker, and a
// queued completion from a superseded lifecycle (for example one shut down and
// then replaced) is recorded but never joins the current worker or clears the
// active flag. cancel() and downloadState() are mutex-protected
// and safe from any thread; shutdown() auto-runs on the owning thread,
// cancels the active download, joins its worker thread, and invalidates any
// queued in-flight starts and completions.
class DownloadService : public QObject {
    Q_OBJECT

public:
    explicit DownloadService(QObject *parent = nullptr);
    explicit DownloadService(std::unique_ptr<IModelDownloader> downloader,
                             QObject *parent = nullptr);
    ~DownloadService() override;

    DownloadService(const DownloadService &) = delete;
    DownloadService &operator=(const DownloadService &) = delete;

    // Request consumed by the next startDownload().
    void setDownloadRequest(const DownloadRequest &request);

    // Reserves the download id synchronously and schedules the start on this
    // object's owning thread. When another download is already active the new
    // id is instead finished Failed with a clear message; the request is never
    // queued behind the active download.
    DownloadId startDownload();

    bool cancel(DownloadId id);
    DownloadState downloadState(DownloadId id) const;

    // Cancels the active download and joins its worker thread. Safe from any
    // thread; executes on the owning thread when called cross-thread. Call
    // before destroying the worker thread this service lives on.
    void shutdown();

signals:
    void downloadStarted(DownloadId id);
    void downloadProgress(DownloadId id, qint64 downloaded_bytes,
                          qint64 total_bytes, double speed_bps, double eta_seconds);
    void downloadFinished(DownloadResult result);

private:
    struct DownloadRecord {
        DownloadId id;
        DownloadState state = DownloadState::Pending;
        // Snapshot of the accepted DownloadRequest, taken under the mutex at
        // reservation time so a later setDownloadRequest() cannot change the
        // work this download performs.
        DownloadRequest request;
        std::shared_ptr<DownloadCancelToken> cancel_token;
        bool cancel_requested = false;
    };

    // Runs on the owning thread: marks the record Running, spawns the worker
    // thread, and then emits downloadStarted. The worker is spawned before the
    // signal so a directly-connected downloadStarted slot that synchronously
    // calls shutdown() or startDownload() can never orphan the worker or
    // overwrite a joinable thread. A reservation whose generation was
    // superseded by a shutdown (or a later lifecycle) while this call was
    // queued becomes a no-op for the worker lifecycle: the reservation is
    // resolved Failed and no worker is spawned, joined, or overwritten.
    void beginDownload(DownloadId id, std::uint64_t reservation_generation);
    // Runs on the owning thread: reaps the completed worker thread, clears the
    // active-download flag, and publishes downloadFinished. A completion whose
    // worker generation no longer matches (superseded lifecycle) only records
    // its outcome and is otherwise discarded.
    void completeDownload(DownloadId id, std::uint64_t worker_generation,
                          const ExecutionResult &result);
    // Runs on the owning thread: publishes a Failed downloadFinished for a
    // request rejected because another download was already active.
    void rejectDownload(DownloadId id, std::string message);

    mutable std::mutex mutex_;
    std::uint64_t next_download_id_ = 1;
    bool active_ = false;
    // Lifecycle generation. Bumped on every worker spawn and on shutdown;
    // each reservation captures the current generation at startDownload() time
    // and every worker carries the generation it was started under. A queued
    // beginDownload from a superseded generation is a no-op, and a stale
    // completion must never join or clear the current lifecycle.
    std::uint64_t generation_ = 0;
    std::unordered_map<std::uint64_t, DownloadRecord> downloads_;
    DownloadRequest request_;
    std::unique_ptr<IModelDownloader> download_executor_;
    std::thread download_thread_;
};
