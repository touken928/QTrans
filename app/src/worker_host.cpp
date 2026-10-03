#include "worker_host.h"

#include "batch/batch_controller.h"
#include "download/download_service.h"
#include "translate/inference_service.h"

#include <QMetaObject>
#include <QThread>

WorkerHost::WorkerHost(const std::filesystem::path &batch_queue_file,
                       const std::filesystem::path &batch_output_dir,
                       QObject *parent)
    : QObject(parent),
      inference_(new InferenceService(this)),
      download_(new DownloadService(this)),
      batch_(new BatchController(inference_, batch_queue_file, batch_output_dir, this)) {
}

InferenceService *WorkerHost::inference() const {
    return inference_;
}

DownloadService *WorkerHost::download() const {
    return download_;
}

BatchController *WorkerHost::batch() const {
    return batch_;
}

void WorkerHost::shutdown() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, &WorkerHost::shutdown,
                                  Qt::BlockingQueuedConnection);
        return;
    }
    download_->shutdown();
    inference_->shutdown();
}
