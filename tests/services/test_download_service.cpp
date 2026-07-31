#include "app/download_service.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

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
    std::atomic<int> calls{0};

    ExecutionResult download(const DownloadRequest &, const DownloadCancelToken *token,
                             DownloadProgressHandler progress) override {
        ++calls;
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

// Throws on the first call (simulating a downloader failure) and completes on
// the second call.
class ThrowThenCompleteDownloader final : public IModelDownloader {
public:
    std::atomic<int> calls{0};

    ExecutionResult download(const DownloadRequest &, const DownloadCancelToken *,
                             DownloadProgressHandler progress) override {
        if (calls.fetch_add(1) == 0) {
            throw std::runtime_error("boom");
        }
        if (progress) progress({1, 1, 1.0, 1.0});
        return {ExecutionOutcome::Completed, {}};
    }
};

// Records the DownloadRequest it receives so a test can prove which request an
// accepted download actually performed.
class RequestRecordingDownloader final : public IModelDownloader {
public:
    std::atomic<int> calls{0};
    std::string seen_local_path;
    std::string seen_remote_spec;

    ExecutionResult download(const DownloadRequest &request, const DownloadCancelToken *,
                             DownloadProgressHandler progress) override {
        ++calls;
        seen_local_path = request.local_path;
        seen_remote_spec = request.remote_spec;
        if (progress) progress({1, 1, 1.0, 1.0});
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
    QMetaObject::invokeMethod(service, &DownloadService::shutdown,
                              Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(context, [&] {
        delete service;
        service = nullptr;
        context->moveToThread(QCoreApplication::instance()->thread()); }, Qt::BlockingQueuedConnection);
    worker.quit();
    worker.wait();
    delete context;
}

TEST(DownloadService, ReentrantDownloadStartedShutdownThenStartDoesNotOrphanWorker) {
    int argc = 1;
    char name[] = "download-reentrant-started-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    auto downloader = std::make_unique<BlockingDownloader>();
    BlockingDownloader *downloader_ptr = downloader.get();
    DownloadService service(std::move(downloader));

    std::vector<DownloadResult> finished;
    bool acted = false;
    DownloadId reentrant_id;
    // Explicitly direct: the slot runs synchronously inside downloadStarted
    // while beginDownload is still on the stack. It shuts the service down and
    // immediately starts a new request; the first download's worker must
    // already be spawned so the reentrant shutdown joins it instead of leaving
    // an orphan that a reentrant start would then overwrite (and terminate).
    QObject::connect(&service, &DownloadService::downloadStarted, &application, [&](DownloadId) {
                         if (acted) return;
                         acted = true;
                         service.shutdown();
                         reentrant_id = service.startDownload(); }, Qt::DirectConnection);
    QObject::connect(&service, &DownloadService::downloadFinished, &application,
                     [&](const DownloadResult &result) { finished.push_back(result); });

    const DownloadId first = service.startDownload();
    EXPECT_TRUE(acted) << "reentrant downloadStarted slot must have fired synchronously";
    EXPECT_TRUE(reentrant_id.is_valid());
    EXPECT_NE(reentrant_id, first);

    // The reentrant download is running; cancel it and drain the loop.
    EXPECT_TRUE(service.cancel(reentrant_id));
    process_until(application, [&] {
        for (const auto &result : finished) {
            if (result.id == reentrant_id) return true;
        }
        return false;
    });

    ASSERT_EQ(finished.size(), 1U);
    EXPECT_EQ(finished[0].id, reentrant_id);
    EXPECT_EQ(finished[0].state, DownloadState::Cancelled);
    // The first download's outcome is recorded (it was shut down inside the
    // slot) but its completion is stale and must not be published.
    EXPECT_EQ(service.downloadState(first), DownloadState::Cancelled);
    EXPECT_EQ(service.downloadState(reentrant_id), DownloadState::Cancelled);
    // Both workers ran and were joined; nothing was orphaned or overwritten.
    EXPECT_EQ(downloader_ptr->calls.load(), 2);
    service.shutdown();
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

TEST(DownloadService, SecondDownloadWhileFirstActiveFinishesFailedPromptly) {
    int argc = 1;
    char name[] = "download-reject-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    auto downloader = std::make_unique<BlockingDownloader>();
    BlockingDownloader *downloader_ptr = downloader.get();
    DownloadService service(std::move(downloader));
    std::vector<DownloadResult> finished;
    bool timer_fired = false;
    QObject::connect(&service, &DownloadService::downloadFinished, &application,
                     [&](const DownloadResult &result) { finished.push_back(result); });

    const DownloadId first = service.startDownload();
    process_until(application, [&] { return downloader_ptr->started.load(); });
    ASSERT_TRUE(downloader_ptr->started.load());

    // A timer that can only fire if the Qt event loop stays responsive while
    // the first download is still running. Under the old serializing behavior
    // the owner thread would block joining the first download and this timer
    // would never fire.
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(50);
    QObject::connect(&timer, &QTimer::timeout, &application, [&] { timer_fired = true; });
    timer.start();

    const auto before = std::chrono::steady_clock::now();
    const DownloadId second = service.startDownload();
    const auto rejected_elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - before)
            .count();
    process_until(application, [&] { return finished.size() == 1; });
    process_until(application, [&] { return timer_fired; });

    // Rejection is prompt and does not join/wait on the owning thread while
    // the first download is still running.
    EXPECT_LT(rejected_elapsed_ms, 500);
    EXPECT_TRUE(timer_fired);
    ASSERT_EQ(finished.size(), 1U);
    EXPECT_EQ(finished[0].id, second);
    EXPECT_EQ(finished[0].state, DownloadState::Failed);
    EXPECT_FALSE(finished[0].error_message.empty());
    EXPECT_EQ(service.downloadState(second), DownloadState::Failed);
    // The first download is untouched by the rejection.
    EXPECT_TRUE(downloader_ptr->started.load());

    // The first download still completes normally after the rejection.
    EXPECT_TRUE(service.cancel(first));
    process_until(application, [&] { return finished.size() == 2; });
    ASSERT_EQ(finished.size(), 2U);
    EXPECT_EQ(finished[1].id, first);
    EXPECT_EQ(finished[1].state, DownloadState::Cancelled);
    service.shutdown();
}

TEST(DownloadService, DownloaderExceptionFinishesFailedAndClearsActiveState) {
    int argc = 1;
    char name[] = "download-exception-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    DownloadService service(std::make_unique<ThrowThenCompleteDownloader>());
    std::vector<DownloadResult> finished;
    QObject::connect(&service, &DownloadService::downloadFinished, &application,
                     [&](const DownloadResult &result) { finished.push_back(result); });

    const DownloadId first = service.startDownload();
    process_until(application, [&] { return finished.size() == 1; });
    ASSERT_EQ(finished.size(), 1U);
    EXPECT_EQ(finished[0].id, first);
    EXPECT_EQ(finished[0].state, DownloadState::Failed);
    EXPECT_FALSE(finished[0].error_message.empty());
    EXPECT_EQ(service.downloadState(first), DownloadState::Failed);

    // The active-download flag was cleared despite the exception: a follow-up
    // download runs normally instead of being rejected as in progress.
    const DownloadId second = service.startDownload();
    process_until(application, [&] { return finished.size() == 2; });
    ASSERT_EQ(finished.size(), 2U);
    EXPECT_EQ(finished[1].id, second);
    EXPECT_EQ(finished[1].state, DownloadState::Completed);
    EXPECT_EQ(service.downloadState(second), DownloadState::Completed);
    service.shutdown();
}

TEST(DownloadService, AcceptedRequestSnapshotIsNotAffectedByLaterSetRequest) {
    int argc = 1;
    char name[] = "download-request-snapshot-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);

    QThread worker;
    worker.start();
    auto *context = new QObject;
    context->moveToThread(&worker);
    DownloadService *service = nullptr;
    auto downloader = std::make_unique<RequestRecordingDownloader>();
    RequestRecordingDownloader *downloader_ptr = downloader.get();
    QMetaObject::invokeMethod(context, [&] { service = new DownloadService(std::move(downloader)); }, Qt::BlockingQueuedConnection);

    // Block the worker's event loop until main has queued the reservation and
    // the later setDownloadRequest(), so beginDownload() is guaranteed to run
    // after both and the snapshot is what the downloader must observe.
    std::atomic<bool> gate_open{false};
    std::atomic<bool> gate_started{false};
    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    QMetaObject::invokeMethod(context, [&] {
        gate_started.store(true);
        std::unique_lock<std::mutex> lock(gate_mutex);
        gate_cv.wait(lock, [&] { return gate_open.load(); }); }, Qt::QueuedConnection);

    bool completed = false;
    QObject::connect(service, &DownloadService::downloadFinished, &application,
                     [&](const DownloadResult &result) {
                         completed = result.state == DownloadState::Completed;
                     });
    process_until(application, [&] { return gate_started.load(); });

    DownloadRequest first;
    first.local_path = "/first";
    first.remote_spec = "remote-first";
    DownloadRequest second;
    second.local_path = "/second";
    second.remote_spec = "remote-second";

    service->setDownloadRequest(first);
    const DownloadId id = service->startDownload();
    service->setDownloadRequest(second);

    gate_open.store(true);
    gate_cv.notify_all();
    process_until(application, [&] { return completed; });

    EXPECT_TRUE(completed);
    EXPECT_EQ(service->downloadState(id), DownloadState::Completed);
    EXPECT_EQ(downloader_ptr->seen_local_path, "/first");
    EXPECT_EQ(downloader_ptr->seen_remote_spec, "remote-first");

    QMetaObject::invokeMethod(service, &DownloadService::shutdown,
                              Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(context, [&] {
        delete service;
        service = nullptr;
        context->moveToThread(QCoreApplication::instance()->thread()); }, Qt::BlockingQueuedConnection);
    worker.quit();
    worker.wait();
    delete context;
}

TEST(DownloadService, StaleCompletionAfterShutdownDoesNotCorruptNextDownload) {
    int argc = 1;
    char name[] = "download-stale-completion-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    auto downloader = std::make_unique<BlockingDownloader>();
    BlockingDownloader *downloader_ptr = downloader.get();
    DownloadService service(std::move(downloader));
    std::vector<DownloadResult> finished;
    QObject::connect(&service, &DownloadService::downloadFinished, &application,
                     [&](const DownloadResult &result) { finished.push_back(result); });

    // Download A runs until shut down; its completion event stays queued and
    // becomes stale once shutdown() bumps the lifecycle generation.
    const DownloadId a = service.startDownload();
    process_until(application, [&] { return downloader_ptr->started.load(); });
    ASSERT_TRUE(downloader_ptr->started.load());
    service.shutdown();
    ASSERT_EQ(downloader_ptr->calls.load(), 1);

    // Subsequent lifecycle: download B runs after the shutdown that killed A.
    const DownloadId b = service.startDownload();
    process_until(application, [&] { return downloader_ptr->calls.load() == 2; });

    // Let the queued stale completion for A be processed while B is still
    // running, then verify it neither joined B nor cleared the active flag.
    bool stale_processed = false;
    QMetaObject::invokeMethod(&application, [&] { stale_processed = true; }, Qt::QueuedConnection);
    process_until(application, [&] { return stale_processed; });

    // The stale completion recorded A's outcome but must not have been
    // published, and B must still be the active download.
    EXPECT_EQ(service.downloadState(a), DownloadState::Cancelled);
    EXPECT_EQ(service.downloadState(b), DownloadState::Running);
    const DownloadId c = service.startDownload();
    EXPECT_EQ(service.downloadState(c), DownloadState::Failed);
    process_until(application, [&] { return finished.size() == 1; });
    ASSERT_EQ(finished.size(), 1U);
    EXPECT_EQ(finished[0].id, c);
    EXPECT_EQ(finished[0].state, DownloadState::Failed);
    EXPECT_FALSE(finished[0].error_message.empty());

    // B completes normally once cancelled.
    EXPECT_TRUE(service.cancel(b));
    process_until(application, [&] {
        for (const auto &result : finished) {
            if (result.id == b) return true;
        }
        return false;
    });
    EXPECT_EQ(service.downloadState(b), DownloadState::Cancelled);
    for (const auto &result : finished) {
        EXPECT_NE(result.id, a) << "stale completion for a superseded download must not be published";
    }
    service.shutdown();
}

TEST(DownloadService, QueuedBeginAfterShutdownIsStaleAndNeverStartsWorker) {
    int argc = 1;
    char name[] = "download-stale-begin-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);

    QThread worker;
    worker.start();
    auto *context = new QObject;
    context->moveToThread(&worker);
    DownloadService *service = nullptr;
    auto downloader = std::make_unique<RequestRecordingDownloader>();
    RequestRecordingDownloader *downloader_ptr = downloader.get();
    QMetaObject::invokeMethod(context, [&] { service = new DownloadService(std::move(downloader)); }, Qt::BlockingQueuedConnection);

    // Deterministically interleave the race: the worker's event loop is held
    // here so beginDownload(A) stays queued, then shutdown() runs on the
    // owning thread BEFORE that queued beginDownload(A) is processed, then a
    // new request B is queued behind it, and only then the loop is released.
    std::atomic<bool> gate_started{false};
    std::atomic<bool> reservation_done{false};
    std::atomic<bool> shutdown_done{false};
    std::atomic<bool> gate_open{false};
    std::mutex gate_mutex;
    std::condition_variable gate_cv;
    QMetaObject::invokeMethod(context, [&] {
        gate_started.store(true);
        std::unique_lock<std::mutex> lock(gate_mutex);
        gate_cv.wait(lock, [&] { return reservation_done.load(); });
        service->shutdown();
        shutdown_done.store(true);
        gate_cv.wait(lock, [&] { return gate_open.load(); }); }, Qt::QueuedConnection);

    std::vector<DownloadId> started_ids;
    std::vector<DownloadResult> finished;
    QObject::connect(service, &DownloadService::downloadStarted, &application,
                     [&](DownloadId id) { started_ids.push_back(id); });
    QObject::connect(service, &DownloadService::downloadFinished, &application,
                     [&](const DownloadResult &result) { finished.push_back(result); });
    process_until(application, [&] { return gate_started.load(); });

    // Reservation A happens before shutdown; its beginDownload stays queued.
    DownloadRequest request_a;
    request_a.local_path = "/a";
    request_a.remote_spec = "remote-a";
    service->setDownloadRequest(request_a);
    const DownloadId a = service->startDownload();

    // Shutdown runs on the owning thread while beginDownload(A) is queued.
    reservation_done.store(true);
    gate_cv.notify_all();
    process_until(application, [&] { return shutdown_done.load(); });

    // A new request is accepted after the shutdown and queued behind A's
    // stale beginDownload.
    DownloadRequest request_b;
    request_b.local_path = "/b";
    request_b.remote_spec = "remote-b";
    service->setDownloadRequest(request_b);
    const DownloadId b = service->startDownload();

    gate_open.store(true);
    gate_cv.notify_all();
    process_until(application, [&] { return finished.size() == 2; });

    // A's queued beginDownload was superseded: it never started a worker,
    // never emitted downloadStarted(A), and never touched B's lifecycle.
    ASSERT_EQ(finished.size(), 2U);
    EXPECT_EQ(finished[0].id, a);
    EXPECT_EQ(finished[0].state, DownloadState::Failed);
    EXPECT_FALSE(finished[0].error_message.empty());
    EXPECT_EQ(finished[1].id, b);
    EXPECT_EQ(finished[1].state, DownloadState::Completed);
    ASSERT_EQ(started_ids.size(), 1U);
    EXPECT_EQ(started_ids.front(), b);
    EXPECT_EQ(service->downloadState(a), DownloadState::Failed);
    EXPECT_EQ(service->downloadState(b), DownloadState::Completed);
    // Exactly one worker ran: B, using its own request snapshot.
    EXPECT_EQ(downloader_ptr->calls.load(), 1);
    EXPECT_EQ(downloader_ptr->seen_local_path, "/b");
    EXPECT_EQ(downloader_ptr->seen_remote_spec, "remote-b");

    QMetaObject::invokeMethod(service, &DownloadService::shutdown,
                              Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(context, [&] {
        delete service;
        service = nullptr;
        context->moveToThread(QCoreApplication::instance()->thread()); }, Qt::BlockingQueuedConnection);
    worker.quit();
    worker.wait();
    delete context;
}
