#pragma once

#include <QObject>

#include <filesystem>

class BatchController;
class DownloadService;
class InferenceService;

// Owns the worker-thread services and their teardown order.
//
// Must be constructed on the worker thread: it Qt-parents InferenceService,
// DownloadService, and BatchController so they are destroyed on that thread
// when WorkerHost is deleted there. LocalApiService is intentionally not a
// child: it stays UI-thread owned. shutdown() joins the download worker and
// stops the model host, in that order, without deleting anything.
class WorkerHost : public QObject {
    Q_OBJECT

public:
    WorkerHost(const std::filesystem::path &batch_queue_file,
               const std::filesystem::path &batch_output_dir,
               QObject *parent = nullptr);

    InferenceService *inference() const;
    DownloadService *download() const;
    BatchController *batch() const;

    // Runs on the worker thread (hops with BlockingQueuedConnection when called
    // from elsewhere). Order: DownloadService::shutdown(), then
    // InferenceService::shutdown(). Does not delete anything.
    void shutdown();

private:
    InferenceService *inference_ = nullptr;
    DownloadService *download_ = nullptr;
    BatchController *batch_ = nullptr;
};
