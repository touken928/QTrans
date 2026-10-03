#include "translate/inference_service.h"
#include "translate/local_api_service.h"
#include "worker_host.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QObject>
#include <QThread>

#include <filesystem>
#include <system_error>

// Exercises the production worker topology end to end: WorkerHost is
// constructed, shut down, and deleted on one worker thread while the
// UI-thread-owned LocalApiService is stopped first and deleted last. The
// bounded wait guarantees the test returns even if the download worker join
// hangs; the thread must exit within the deadline.
TEST(WorkerHost, ShutsDownAndDeletesServicesOnWorkerThread) {
    int argc = 1;
    char name[] = "worker-host-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);

    const auto dir = std::filesystem::temp_directory_path() / "qtrans-worker-host-test";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    QThread worker;
    worker.start();
    auto *context = new QObject;
    context->moveToThread(&worker);

    WorkerHost *host = nullptr;
    QMetaObject::invokeMethod(context, [&] { host = new WorkerHost(dir / "queue.bq", dir / "output"); }, Qt::BlockingQueuedConnection);
    ASSERT_NE(host, nullptr);

    // UI-thread owned, exactly as in main: stop it before the worker services.
    auto *local_api_service = new LocalApiService(host->inference()->apiChat());
    local_api_service->stop();

    // Same order as main: WorkerHost::shutdown first (DownloadService then
    // InferenceService), then delete the host (children die with it) on the
    // worker thread.
    QMetaObject::invokeMethod(host, &WorkerHost::shutdown,
                              Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(context, [&] {
        delete host;
        host = nullptr;
        context->moveToThread(QCoreApplication::instance()->thread()); }, Qt::BlockingQueuedConnection);

    worker.quit();
    const bool finished = worker.wait(5000);
    if (!finished) {
        worker.terminate();
        worker.wait(1000);
    }
    if (finished) {
        delete context;
    }
    delete local_api_service;

    EXPECT_TRUE(finished) << "worker thread did not finish within the deadline";
}
