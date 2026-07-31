#include "app/download_service.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QThread>

#include <chrono>
#include <functional>
#include <atomic>
#include <thread>

namespace {

void process_until(QCoreApplication &application, const std::function<bool()> &condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition() && std::chrono::steady_clock::now() < deadline) {
        application.processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::yield();
    }
}

class BlockingDownloader final : public IModelDownloader {
public:
    std::atomic<bool> started{false};

    ExecutionResult download(const DownloadRequest &, const DownloadCancelToken *token,
                             DownloadProgressHandler progress) override {
        started.store(true);
        while (!token->is_cancelled()) {
            if (progress) progress({1, 2, 3.0, 1.0});
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return {ExecutionOutcome::Cancelled, "download cancelled"};
    }
};

class CompletingDownloader final : public IModelDownloader {
public:
    ExecutionResult download(const DownloadRequest &, const DownloadCancelToken *,
                             DownloadProgressHandler progress) override {
        if (progress) progress({10, 20, 5.0, 2.0});
        return {ExecutionOutcome::Completed, {}};
    }
};

// Call 1 emits two progress pulses (delayed) and completes; call 2 emits one
// pulse and completes. The payload encodes the producing download so a stale
// late pulse can be identified.
class PulseDownloader final : public IModelDownloader {
public:
    std::atomic<int> calls{0};

    ExecutionResult download(const DownloadRequest &, const DownloadCancelToken *,
                             DownloadProgressHandler progress) override {
        const int call = calls.fetch_add(1) + 1;
        if (progress) progress({call * 100, 1000, 1.0, 1.0});
        if (call == 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            if (progress) progress({call * 100, 1000, 1.0, 1.0});
        }
        return {ExecutionOutcome::Completed, {}};
    }
};

}  // namespace

TEST(DownloadService, CancellationDoesNotBlockQtEventLoop) {
    int argc = 1;
    char name[] = "download-service-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    auto downloader = std::make_unique<BlockingDownloader>();
    BlockingDownloader *downloader_ptr = downloader.get();
    DownloadService service(std::move(downloader));
    bool cancelled = false;
    QObject::connect(&service, &DownloadService::downloadFinished, &application,
                     [&](const DownloadResult &result) {
                         cancelled = result.state == DownloadState::Cancelled;
                     });
    const DownloadId id = service.startDownload();
    EXPECT_TRUE(id.is_valid());
    process_until(application, [&] { return downloader_ptr->started.load(); });
    ASSERT_TRUE(downloader_ptr->started.load());
    EXPECT_TRUE(service.cancel(id));
    process_until(application, [&] { return cancelled; });
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(service.downloadState(id), DownloadState::Cancelled);
    service.shutdown();
}

TEST(DownloadService, CompletesAndReportsProgressAndStarted) {
    int argc = 1;
    char name[] = "download-service-complete-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    DownloadService service(std::make_unique<CompletingDownloader>());
    std::vector<DownloadId> started_ids;
    bool saw_progress = false;
    bool completed = false;
    QObject::connect(&service, &DownloadService::downloadStarted, &application,
                     [&](DownloadId id) { started_ids.push_back(id); });
    QObject::connect(&service, &DownloadService::downloadProgress, &application,
                     [&](DownloadId, qint64 downloaded, qint64 total, double speed, double eta) {
                         saw_progress = downloaded == 10 && total == 20 && speed > 0.0 && eta > 0.0;
                     });
    QObject::connect(&service, &DownloadService::downloadFinished, &application,
                     [&](const DownloadResult &result) {
                         completed = result.state == DownloadState::Completed;
                     });
    const DownloadId id = service.startDownload();
    process_until(application, [&] { return completed; });
    EXPECT_TRUE(completed);
    EXPECT_TRUE(saw_progress);
    ASSERT_EQ(started_ids.size(), 1U);
    EXPECT_EQ(started_ids.front(), id);
    EXPECT_EQ(service.downloadState(id), DownloadState::Completed);
    service.shutdown();
}

TEST(DownloadService, WorkerTopologyShutsDownOrderly) {
    int argc = 1;
    char name[] = "download-service-thread-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    QThread worker;
    worker.start();
    auto *context = new QObject;
    context->moveToThread(&worker);
    DownloadService *service = nullptr;
    QMetaObject::invokeMethod(context, [&] { service = new DownloadService(std::make_unique<CompletingDownloader>()); }, Qt::BlockingQueuedConnection);
    bool completed = false;
    QObject::connect(service, &DownloadService::downloadFinished, &application,
                     [&](const DownloadResult &result) {
                         completed = result.state == DownloadState::Completed;
                     });
    service->startDownload();
    process_until(application, [&] { return completed; });
    ASSERT_TRUE(completed);
    QMetaObject::invokeMethod(service, &DownloadService::shutdown, Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(context, [&] {
        delete service;
        service = nullptr;
        context->moveToThread(QCoreApplication::instance()->thread()); }, Qt::BlockingQueuedConnection);
    worker.quit();
    worker.wait();
    delete context;
}

TEST(DownloadService, ConsecutiveDownloadsCorrelateEventsToTheirOwnIds) {
    int argc = 1;
    char name[] = "download-consecutive-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    DownloadService service(std::make_unique<PulseDownloader>());
    std::vector<DownloadId> started_order;
    std::vector<DownloadId> finished_order;
    std::vector<std::pair<DownloadId, qint64>> progress_events;
    QObject::connect(&service, &DownloadService::downloadStarted, &application,
                     [&](DownloadId id) { started_order.push_back(id); });
    QObject::connect(&service, &DownloadService::downloadProgress, &application,
                     [&](DownloadId id, qint64 downloaded, qint64, double, double) {
                         progress_events.emplace_back(id, downloaded);
                     });
    QObject::connect(&service, &DownloadService::downloadFinished, &application,
                     [&](const DownloadResult &result) { finished_order.push_back(result.id); });

    const DownloadId a = service.startDownload();
    process_until(application, [&] { return finished_order.size() == 1; });
    const DownloadId b = service.startDownload();
    process_until(application, [&] { return finished_order.size() == 2; });

    ASSERT_EQ(started_order.size(), 2U);
    EXPECT_EQ(started_order[0], a);
    EXPECT_EQ(started_order[1], b);
    ASSERT_EQ(finished_order.size(), 2U);
    EXPECT_EQ(finished_order[0], a);
    EXPECT_EQ(finished_order[1], b);
    for (const auto &[id, payload] : progress_events) {
        if (payload == 100) {
            EXPECT_EQ(id, a);
        } else if (payload == 200) {
            EXPECT_EQ(id, b);
        } else {
            ADD_FAILURE() << "unexpected progress payload " << payload;
        }
    }
    EXPECT_EQ(service.downloadState(a), DownloadState::Completed);
    EXPECT_EQ(service.downloadState(b), DownloadState::Completed);
    service.shutdown();
}

TEST(DownloadService, LateProgressFromEarlierDownloadKeepsItsOwnId) {
    int argc = 1;
    char name[] = "download-stale-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    DownloadService service(std::make_unique<PulseDownloader>());
    std::vector<std::pair<DownloadId, qint64>> progress_events;
    std::vector<DownloadId> finished_order;
    QObject::connect(&service, &DownloadService::downloadProgress, &application,
                     [&](DownloadId id, qint64 downloaded, qint64, double, double) {
                         progress_events.emplace_back(id, downloaded);
                     });
    QObject::connect(&service, &DownloadService::downloadFinished, &application,
                     [&](const DownloadResult &result) { finished_order.push_back(result.id); });

    const DownloadId a = service.startDownload();
    process_until(application, [&] { return progress_events.size() == 1; });
    // Starting the second download joins the first download's thread; the
    // first download's late progress pulse is delivered afterwards while the
    // second download is running and must still carry the first id.
    const DownloadId b = service.startDownload();
    process_until(application, [&] { return finished_order.size() == 2; });

    ASSERT_GE(progress_events.size(), 2U);
    for (const auto &[id, payload] : progress_events) {
        if (payload == 100) {
            EXPECT_EQ(id, a);
        } else if (payload == 200) {
            EXPECT_EQ(id, b);
        } else {
            ADD_FAILURE() << "unexpected progress payload " << payload;
        }
    }
    ASSERT_EQ(finished_order.size(), 2U);
    EXPECT_EQ(finished_order[0], a);
    EXPECT_EQ(finished_order[1], b);
    service.shutdown();
}
