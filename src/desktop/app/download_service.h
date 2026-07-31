#pragma once

#include "domain/download/download_types.h"
#include "domain/download/model_downloader.h"
#include "domain/download/download_cancellation.h"

#include <QMetaType>
#include <QObject>

#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

Q_DECLARE_METATYPE(DownloadId)
Q_DECLARE_METATYPE(DownloadState)
Q_DECLARE_METATYPE(DownloadResult)

// Sole owner of dedicated model download execution. Runs downloads on a
// dedicated std::thread with a shared DownloadCancelToken; progress and completion are
// reposted to this object and republished as typed signals carrying the exact
// source DownloadId. Has no ModelHost dependency; MainWindow coordinates
// download completion with inference load.
//
// Threading: startDownload() reserves the DownloadId synchronously from any
// thread, then queues the actual start (thread join/spawn, Running state, and
// downloadStarted delivery) onto this object's owning thread. cancel() and
// downloadState() are mutex-protected and safe from any thread; shutdown()
// auto-runs on the owning thread.
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
    // object's owning thread. The download itself runs on a dedicated thread
    // and never blocks the Qt event loop.
    DownloadId startDownload();

    bool cancel(DownloadId id);
    DownloadState downloadState(DownloadId id) const;

    // Joins the dedicated download thread. Safe from any thread; executes on
    // the owning thread when called cross-thread. Call before destroying the
    // worker thread this service lives on.
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
        std::shared_ptr<DownloadCancelToken> cancel_token;
        bool cancel_requested = false;
    };

    // Runs on the owning thread: joins a previous download, marks the record
    // Running, emits downloadStarted, and spawns the dedicated thread.
    void beginDownload(DownloadId id);
    void completeDownload(DownloadId id, const ExecutionResult &result);

    mutable std::mutex mutex_;
    std::uint64_t next_download_id_ = 1;
    std::unordered_map<std::uint64_t, DownloadRecord> downloads_;
    DownloadRequest request_;
    std::unique_ptr<IModelDownloader> download_executor_;
    std::thread download_thread_;
};
