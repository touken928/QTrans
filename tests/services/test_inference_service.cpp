#include "app/inference_service.h"
#include "app/batch_controller.h"
#include "domain/inference/runtime_capabilities.h"
#include "model_host_test_access.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QThread>

#include <chrono>
#include <filesystem>
#include <functional>
#include <atomic>
#include <system_error>
#include <thread>
#include <fstream>
#include <vector>

namespace {

void process_until(QCoreApplication &application, const std::function<bool()> &condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition() && std::chrono::steady_clock::now() < deadline) {
        application.processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::yield();
    }
}

NativeTranslationRequest native_request() {
    NativeTranslationRequest request;
    request.source = "hello";
    request.target_language = "English";
    request.source_language = "Auto";
    return request;
}

struct ConsumerLog {
    TranslationJobId own;
    TranslationJobId active;  // synchronously returned id; never reassigned
    std::vector<TranslationJobId> started;
    std::vector<TranslationJobId> deltas;
    std::vector<TranslationJobId> finished;
    bool foreign_started_seen = false;

    ConsumerLog(TranslationJobId own_id, TranslationJobId active_id)
        : own(own_id), active(active_id) {
    }
};

// Mimics the fixed consumer behavior (MainWindow/SessionController): the
// active id is the synchronously returned job id and is never overwritten by
// a translationStarted event; started/delta/finish are accepted only for the
// matching job and foreign events are ignored.
void watch_job(InferenceService &service, QCoreApplication &application, ConsumerLog &log) {
    QObject::connect(&service, &InferenceService::translationStarted, &application,
                     [&log](TranslationJobId id) {
                         if (id != log.own) {
                             log.foreign_started_seen = true;
                             return;
                         }
                         log.started.push_back(id);
                     });
    QObject::connect(&service, &InferenceService::translationDelta, &application,
                     [&log](TranslationJobId id, TranslationChannel, const QString &) {
                         if (id == log.own) log.deltas.push_back(id);
                     });
    QObject::connect(&service, &InferenceService::translationFinished, &application,
                     [&log](const TranslationJobResult &result) {
                         if (result.id == log.own) log.finished.push_back(result.id);
                     });
}

}  // namespace

TEST(InferenceService, MapsLoadAndTranslationThroughModelHost) {
    int argc = 1;
    char name[] = "inference-service-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(qtrans::core::test::ModelHostHooks{});
    InferenceService service;
    service.setModelConfig(QStringLiteral("demo"), QString());

    bool loaded = false;
    bool translated = false;
    QObject::connect(&service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    QObject::connect(&service, &InferenceService::translationFinished, &application,
                     [&](const TranslationJobResult &result) {
                         translated = result.state == TranslationState::Completed;
                     });

    service.loadModel();
    process_until(application, [&] { return loaded; });
    ASSERT_TRUE(loaded);
    const TranslationJobId id = service.translateNative(native_request());
    EXPECT_TRUE(id.is_valid());
    process_until(application, [&] { return translated; });
    EXPECT_TRUE(translated);
    EXPECT_EQ(service.jobState(id), TranslationState::Completed);
    service.shutdown();
}

TEST(InferenceService, CancellationAndBatchPreemptionPreserveStates) {
    int argc = 1;
    char name[] = "inference-service-cancel-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    qtrans::core::test::ModelHostHooks hooks;
    hooks.generate = [](std::string_view, const qtrans::core::SamplingOptions &,
                        const std::function<void(std::string_view)> &emit_piece,
                        const std::function<bool()> &stop) {
        emit_piece("partial");
        while (!stop()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return qtrans::core::test::TestGeneration{"partial", 1, 1, false, {}};
    };
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(hooks);
    InferenceService service;
    service.setModelConfig(QStringLiteral("demo"), QString());
    bool loaded = false;
    QObject::connect(&service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    service.loadModel();
    process_until(application, [&] { return loaded; });
    ASSERT_TRUE(loaded);

    bool cancelled = false;
    QObject::connect(&service, &InferenceService::translationFinished, &application,
                     [&](const TranslationJobResult &result) {
                         cancelled = result.state == TranslationState::Cancelled;
                     });
    const TranslationJobId interactive = service.translateNative(native_request());
    process_until(application, [&] { return service.jobState(interactive) == TranslationState::Running; });
    EXPECT_TRUE(service.cancel(interactive));
    process_until(application, [&] { return cancelled; });
    EXPECT_TRUE(cancelled);

    bool preempted = false;
    QObject::connect(&service, &InferenceService::translationFinished, &application,
                     [&](const TranslationJobResult &result) {
                         if (result.id != interactive)
                             preempted = result.state == TranslationState::Preempted;
                     });
    BatchTranslationRequest batch;
    batch.source = "batch";
    batch.target_language = "English";
    batch.source_language = "Auto";
    const TranslationJobId batch_job = service.translateBatch(batch);
    process_until(application, [&] { return service.jobState(batch_job) == TranslationState::Running; });
    EXPECT_TRUE(service.preemptBatch());
    process_until(application, [&] { return preempted; });
    EXPECT_TRUE(preempted);
    service.shutdown();
}

TEST(InferenceService, CancellationStaysStickyAcrossBackTranslationHandoff) {
    int argc = 1;
    char name[] = "inference-service-back-cancel-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    std::atomic<int> calls{0};
    qtrans::core::test::ModelHostHooks hooks;
    hooks.generate = [&calls](std::string_view, const qtrans::core::SamplingOptions &,
                              const std::function<void(std::string_view)> &emit_piece,
                              const std::function<bool()> &) {
        ++calls;
        emit_piece("forward");
        return qtrans::core::test::TestGeneration{"forward", 1, 1, false, {}};
    };
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(hooks);
    InferenceService service;
    service.setModelConfig(QStringLiteral("demo"), QString());
    bool loaded = false;
    bool cancelled = false;
    QObject::connect(&service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    service.loadModel();
    process_until(application, [&] { return loaded; });
    ASSERT_TRUE(loaded);
    auto request = native_request();
    request.back_translate = true;
    const TranslationJobId id = service.translateNative(request);
    QObject::connect(&service, &InferenceService::translationDelta, &application,
                     [&](TranslationJobId job_id, TranslationChannel channel, const QString &) {
                         if (job_id == id && channel == TranslationChannel::Target)
                             service.cancel(id);
                     });
    QObject::connect(&service, &InferenceService::translationFinished, &application,
                     [&](const TranslationJobResult &result) {
                         if (result.id == id) cancelled = result.state == TranslationState::Cancelled;
                     });
    process_until(application, [&] { return cancelled; });
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(calls.load(), 1);
    service.shutdown();
}

TEST(InferenceService, UnloadAndShutdownAreExplicit) {
    int argc = 1;
    char name[] = "inference-service-shutdown-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(qtrans::core::test::ModelHostHooks{});
    InferenceService service;
    service.setModelConfig(QStringLiteral("demo"), QString());
    bool loaded = false;
    bool unloaded = false;
    QObject::connect(&service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    QObject::connect(&service, &InferenceService::modelUnloadFinished, &application,
                     [&] { unloaded = true; });
    service.loadModel();
    process_until(application, [&] { return loaded; });
    ASSERT_TRUE(loaded);
    service.unloadModel();
    process_until(application, [&] { return unloaded; });
    EXPECT_TRUE(unloaded);
    EXPECT_FALSE(service.isModelLoaded());
    service.shutdown();
}

TEST(InferenceService, EventsAreSequencedInOrder) {
    int argc = 1;
    char name[] = "inference-service-order-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    qtrans::core::test::ModelHostHooks hooks;
    hooks.generate = [](std::string_view, const qtrans::core::SamplingOptions &,
                        const std::function<void(std::string_view)> &emit_piece,
                        const std::function<bool()> &) {
        emit_piece("a");
        emit_piece("b");
        emit_piece("c");
        return qtrans::core::test::TestGeneration{"abc", 1, 3, false, {}};
    };
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(hooks);
    InferenceService service;
    service.setModelConfig(QStringLiteral("demo"), QString());
    bool loaded = false;
    QObject::connect(&service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    service.loadModel();
    process_until(application, [&] { return loaded; });
    ASSERT_TRUE(loaded);

    std::vector<std::string> events;
    QObject::connect(&service, &InferenceService::translationStarted, &application,
                     [&](TranslationJobId) { events.emplace_back("started"); });
    QObject::connect(&service, &InferenceService::translationReset, &application,
                     [&](TranslationJobId, TranslationChannel) { events.emplace_back("reset"); });
    QObject::connect(&service, &InferenceService::translationDelta, &application,
                     [&](TranslationJobId, TranslationChannel, const QString &piece) {
                         events.emplace_back("delta:" + piece.toStdString());
                     });
    QObject::connect(&service, &InferenceService::translationFinished, &application,
                     [&](const TranslationJobResult &) { events.emplace_back("finished"); });
    service.translateNative(native_request());
    process_until(application, [&] { return !events.empty() && events.back() == "finished"; });
    const std::vector<std::string> expected = {"started", "reset", "delta:a", "delta:b", "delta:c", "finished"};
    EXPECT_EQ(events, expected);
    service.shutdown();
}

TEST(InferenceService, BackendInitializationRefreshesCapabilities) {
    int argc = 1;
    char name[] = "inference-service-backend-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    InferenceService service;
    service.initializeBackend();
    EXPECT_FALSE(RuntimeCapabilities::instance().environment().label.empty());
    service.shutdown();
}

TEST(InferenceService, WorkerTopologyDrainsQueuedEventsBeforeShutdown) {
    int argc = 1;
    char name[] = "inference-service-thread-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    QThread worker;
    worker.start();
    auto *context = new QObject;
    context->moveToThread(&worker);
    InferenceService *service = nullptr;
    QMetaObject::invokeMethod(context, [&] {
        qtrans::core::test::ScopedModelHostHooks worker_hooks(qtrans::core::test::ModelHostHooks{});
        service = new InferenceService; }, Qt::BlockingQueuedConnection);
    bool loaded = false;
    QObject::connect(service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    service->setModelConfig(QStringLiteral("demo"), QString());
    service->loadModel();
    process_until(application, [&] { return loaded; });
    ASSERT_TRUE(loaded);
    QMetaObject::invokeMethod(service, &InferenceService::shutdown, Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(context, [&] {
        delete service;
        service = nullptr;
        context->moveToThread(QCoreApplication::instance()->thread()); }, Qt::BlockingQueuedConnection);
    worker.quit();
    worker.wait();
    delete context;
}

TEST(InferenceService, BatchPreemptionRequeuesOnWorkerTimer) {
    int argc = 1;
    char name[] = "batch-thread-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    std::atomic<int> calls{0};
    qtrans::core::test::ModelHostHooks hooks;
    hooks.generate = [&calls](std::string_view, const qtrans::core::SamplingOptions &,
                              const std::function<void(std::string_view)> &emit_piece,
                              const std::function<bool()> &stop) {
        const int call = ++calls;
        if (call == 1) {
            emit_piece("partial");
            while (!stop()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return qtrans::core::test::TestGeneration{"done", 1, 1, false, {}};
    };
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(hooks);
    const auto input = std::filesystem::temp_directory_path() / "qtrans-batch-requeue.txt";
    {
        std::ofstream file(input);
        file << "batch text";
    }
    QThread worker;
    worker.start();
    auto *context = new QObject;
    context->moveToThread(&worker);
    InferenceService *service = nullptr;
    BatchController *batch = nullptr;
    QMetaObject::invokeMethod(context, [&] {
        qtrans::core::test::ScopedModelHostHooks worker_hooks(hooks);
        service = new InferenceService;
        service->setModelConfig(QStringLiteral("demo"), QString());
        batch = new BatchController(service, input.string() + ".queue", input.parent_path()); }, Qt::BlockingQueuedConnection);
    bool loaded = false;
    bool finished = false;
    bool preempt_requested = false;
    bool batch_submitted = false;
    QObject::connect(service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    QObject::connect(service, &InferenceService::translationStarted, &application,
                     [&](TranslationJobId) {
                         if (batch_submitted && !preempt_requested) {
                             preempt_requested = true;
                             service->translateNative(native_request());
                         }
                     });
    QObject::connect(batch, &BatchController::batchFinished, &application, [&] { finished = true; });
    service->loadModel();
    process_until(application, [&] { return loaded; });
    ASSERT_TRUE(loaded);
    QMetaObject::invokeMethod(batch, "addFile", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(input.string())),
                              Q_ARG(QString, QStringLiteral("Auto")),
                              Q_ARG(QString, QStringLiteral("English")));
    batch_submitted = true;
    QMetaObject::invokeMethod(batch, "start", Qt::QueuedConnection);
    process_until(application, [&] { return finished; });
    EXPECT_TRUE(finished);
    EXPECT_GE(calls.load(), 3);
    QMetaObject::invokeMethod(service, &InferenceService::shutdown, Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(context, [&] {
        delete batch;
        delete service;
        context->moveToThread(QCoreApplication::instance()->thread()); }, Qt::BlockingQueuedConnection);
    worker.quit();
    worker.wait();
    delete context;
    std::error_code error;
    std::filesystem::remove(input, error);
    std::filesystem::remove(input.string() + ".queue", error);
}

TEST(InferenceService, RemovingActiveBatchEntryAdvancesQueue) {
    int argc = 1;
    char name[] = "batch-remove-active-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    std::atomic<int> calls{0};
    qtrans::core::test::ModelHostHooks hooks;
    hooks.generate = [&calls](std::string_view, const qtrans::core::SamplingOptions &,
                              const std::function<void(std::string_view)> &emit_piece,
                              const std::function<bool()> &stop) {
        const int call = ++calls;
        if (call == 1) {
            // First job stays in-flight until the controller cancels it when
            // its entry is removed mid-run.
            emit_piece("partial");
            while (!stop()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return qtrans::core::test::TestGeneration{"done", 1, 1, false, {}};
    };
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(hooks);
    const auto dir = std::filesystem::temp_directory_path() / "qtrans-batch-remove-active";
    const auto input_a = dir / "a.txt";
    const auto input_b = dir / "b.txt";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    {
        std::ofstream file(input_a);
        file << "first file";
    }
    {
        std::ofstream file(input_b);
        file << "second file";
    }
    QThread worker;
    worker.start();
    auto *context = new QObject;
    context->moveToThread(&worker);
    InferenceService *service = nullptr;
    BatchController *batch = nullptr;
    QMetaObject::invokeMethod(context, [&] {
        qtrans::core::test::ScopedModelHostHooks worker_hooks(hooks);
        service = new InferenceService;
        service->setModelConfig(QStringLiteral("demo"), QString());
        batch = new BatchController(service, (dir / "queue.bq").string(), dir); }, Qt::BlockingQueuedConnection);

    QString id_a;
    QString id_b;
    bool loaded = false;
    bool finished = false;
    bool removed = false;
    bool last_running = true;
    QObject::connect(service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    QObject::connect(batch, &BatchController::entryAdded, &application,
                     [&](const QString &id, const QString &, const QString &) {
                         if (id_a.isEmpty())
                             id_a = id;
                         else
                             id_b = id;
                     });
    QObject::connect(batch, &BatchController::entryRemoved, &application,
                     [&](const QString &id) { if (id == id_a) removed = true; });
    QObject::connect(batch, &BatchController::batchStateChanged, &application,
                     [&](bool running, bool) { last_running = running; });
    QObject::connect(batch, &BatchController::batchFinished, &application, [&] { finished = true; });
    service->loadModel();
    process_until(application, [&] { return loaded; });
    ASSERT_TRUE(loaded);

    QMetaObject::invokeMethod(batch, "addFile", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(input_a.string())),
                              Q_ARG(QString, QStringLiteral("Auto")),
                              Q_ARG(QString, QStringLiteral("English")));
    QMetaObject::invokeMethod(batch, "addFile", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(input_b.string())),
                              Q_ARG(QString, QStringLiteral("Auto")),
                              Q_ARG(QString, QStringLiteral("English")));

    bool removal_requested = false;
    QObject::connect(service, &InferenceService::translationStarted, &application,
                     [&](TranslationJobId) {
                         if (removal_requested) return;
                         removal_requested = true;
                         QMetaObject::invokeMethod(batch, "removeEntry", Qt::QueuedConnection,
                                                   Q_ARG(QString, id_a));
                     });
    QMetaObject::invokeMethod(batch, "start", Qt::QueuedConnection);
    process_until(application, [&] { return finished; });

    EXPECT_TRUE(removed);
    // First entry was aborted mid-run (call 1); second entry ran to completion.
    EXPECT_EQ(calls.load(), 2);
    EXPECT_EQ(batch->entryIds().size(), 1);
    EXPECT_EQ(batch->entryState(id_a), -1);
    EXPECT_EQ(batch->entryState(id_b), static_cast<int>(BatchEntryState::Completed));
    EXPECT_FALSE(last_running);
    EXPECT_TRUE(std::filesystem::exists(dir / "b_translated.txt"));
    EXPECT_FALSE(std::filesystem::exists(dir / "a_translated.txt"));

    QMetaObject::invokeMethod(service, &InferenceService::shutdown, Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(context, [&] {
        delete batch;
        delete service;
        context->moveToThread(QCoreApplication::instance()->thread()); }, Qt::BlockingQueuedConnection);
    worker.quit();
    worker.wait();
    delete context;
    std::filesystem::remove_all(dir, ec);
}

TEST(InferenceService, RemovingActiveBatchEntryWaitsForOldJobTerminal) {
    int argc = 1;
    char name[] = "batch-remove-wait-terminal-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    std::atomic<int> calls{0};
    std::atomic<bool> release_first{false};
    qtrans::core::test::ModelHostHooks hooks;
    hooks.generate = [&](std::string_view, const qtrans::core::SamplingOptions &,
                         const std::function<void(std::string_view)> &,
                         const std::function<bool()> &) {
        const int call = ++calls;
        if (call == 1) {
            // The first job stays in-flight until the test releases it, even
            // after the controller cancels it, so the window in which the old
            // job has no terminal event yet is deterministic.
            while (!release_first.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return qtrans::core::test::TestGeneration{"done", 1, 1, false, {}};
    };
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(hooks);
    const auto dir = std::filesystem::temp_directory_path() / "qtrans-batch-remove-wait-terminal";
    const auto input_a = dir / "a.txt";
    const auto input_b = dir / "b.txt";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    {
        std::ofstream file(input_a);
        file << "first file";
    }
    {
        std::ofstream file(input_b);
        file << "second file";
    }
    QThread worker;
    worker.start();
    auto *context = new QObject;
    context->moveToThread(&worker);
    InferenceService *service = nullptr;
    BatchController *batch = nullptr;
    QMetaObject::invokeMethod(context, [&] {
        qtrans::core::test::ScopedModelHostHooks worker_hooks(hooks);
        service = new InferenceService;
        service->setModelConfig(QStringLiteral("demo"), QString());
        batch = new BatchController(service, (dir / "queue.bq").string(), dir); }, Qt::BlockingQueuedConnection);

    QString id_a;
    QString id_b;
    bool loaded = false;
    bool finished = false;
    bool removal_requested = false;
    QObject::connect(service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    QObject::connect(batch, &BatchController::entryAdded, &application,
                     [&](const QString &id, const QString &, const QString &) {
                         if (id_a.isEmpty())
                             id_a = id;
                         else
                             id_b = id;
                     });
    QObject::connect(batch, &BatchController::batchFinished, &application, [&] { finished = true; });
    service->loadModel();
    process_until(application, [&] { return loaded; });
    ASSERT_TRUE(loaded);

    QMetaObject::invokeMethod(batch, "addFile", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(input_a.string())),
                              Q_ARG(QString, QStringLiteral("Auto")),
                              Q_ARG(QString, QStringLiteral("English")));
    QMetaObject::invokeMethod(batch, "addFile", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(input_b.string())),
                              Q_ARG(QString, QStringLiteral("Auto")),
                              Q_ARG(QString, QStringLiteral("English")));
    QObject::connect(service, &InferenceService::translationStarted, &application,
                     [&](TranslationJobId) {
                         if (removal_requested) return;
                         removal_requested = true;
                         QMetaObject::invokeMethod(batch, "removeEntry", Qt::QueuedConnection,
                                                   Q_ARG(QString, id_a));
                     });
    QMetaObject::invokeMethod(batch, "start", Qt::QueuedConnection);

    // Wait until removeEntry was processed (entry A is gone from the store).
    process_until(application, [&] {
        return !id_a.isEmpty() && batch->entryState(id_a) == -1;
    });

    // The old job is still blocked on the gate, so its terminal event has not
    // been observed yet: the next entry must not have been submitted.
    EXPECT_EQ(batch->entryState(id_b), static_cast<int>(BatchEntryState::Queued));

    // Release the old job; its terminal is consumed and only then does the
    // queue advance to entry B.
    release_first = true;
    process_until(application, [&] { return finished; });
    EXPECT_EQ(calls.load(), 2);
    EXPECT_EQ(batch->entryState(id_a), -1);
    EXPECT_EQ(batch->entryState(id_b), static_cast<int>(BatchEntryState::Completed));
    EXPECT_TRUE(std::filesystem::exists(dir / "b_translated.txt"));
    EXPECT_FALSE(std::filesystem::exists(dir / "a_translated.txt"));

    QMetaObject::invokeMethod(service, &InferenceService::shutdown, Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(context, [&] {
        delete batch;
        delete service;
        context->moveToThread(QCoreApplication::instance()->thread()); }, Qt::BlockingQueuedConnection);
    worker.quit();
    worker.wait();
    delete context;
    std::filesystem::remove_all(dir, ec);
}

TEST(InferenceService, RemovingActiveBatchEntryWhilePausedResumesQueue) {
    int argc = 1;
    char name[] = "batch-remove-paused-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    std::atomic<int> calls{0};
    qtrans::core::test::ModelHostHooks hooks;
    hooks.generate = [&calls](std::string_view, const qtrans::core::SamplingOptions &,
                              const std::function<void(std::string_view)> &emit_piece,
                              const std::function<bool()> &stop) {
        const int call = ++calls;
        if (call == 1) {
            emit_piece("partial");
            while (!stop()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return qtrans::core::test::TestGeneration{"done", 1, 1, false, {}};
    };
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(hooks);
    const auto dir = std::filesystem::temp_directory_path() / "qtrans-batch-remove-paused";
    const auto input_a = dir / "a.txt";
    const auto input_b = dir / "b.txt";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    {
        std::ofstream file(input_a);
        file << "first file";
    }
    {
        std::ofstream file(input_b);
        file << "second file";
    }
    QThread worker;
    worker.start();
    auto *context = new QObject;
    context->moveToThread(&worker);
    InferenceService *service = nullptr;
    BatchController *batch = nullptr;
    QMetaObject::invokeMethod(context, [&] {
        qtrans::core::test::ScopedModelHostHooks worker_hooks(hooks);
        service = new InferenceService;
        service->setModelConfig(QStringLiteral("demo"), QString());
        batch = new BatchController(service, (dir / "queue.bq").string(), dir); }, Qt::BlockingQueuedConnection);

    QString id_a;
    QString id_b;
    bool loaded = false;
    bool started = false;
    bool paused_seen = false;
    bool finished = false;
    bool last_running = true;
    QObject::connect(service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    QObject::connect(batch, &BatchController::entryAdded, &application,
                     [&](const QString &id, const QString &, const QString &) {
                         if (id_a.isEmpty())
                             id_a = id;
                         else
                             id_b = id;
                     });
    QObject::connect(service, &InferenceService::translationStarted, &application,
                     [&](TranslationJobId) { started = true; });
    QObject::connect(batch, &BatchController::batchStateChanged, &application,
                     [&](bool running, bool paused) {
                         last_running = running;
                         if (running && paused) paused_seen = true;
                     });
    QObject::connect(batch, &BatchController::batchFinished, &application, [&] { finished = true; });
    service->loadModel();
    process_until(application, [&] { return loaded; });
    ASSERT_TRUE(loaded);

    QMetaObject::invokeMethod(batch, "addFile", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(input_a.string())),
                              Q_ARG(QString, QStringLiteral("Auto")),
                              Q_ARG(QString, QStringLiteral("English")));
    QMetaObject::invokeMethod(batch, "addFile", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(input_b.string())),
                              Q_ARG(QString, QStringLiteral("Auto")),
                              Q_ARG(QString, QStringLiteral("English")));
    QMetaObject::invokeMethod(batch, "start", Qt::QueuedConnection);
    process_until(application, [&] { return started; });

    // Pause the running batch, remove the active entry, then resume: the
    // remaining queue item must be processed only after resume.
    QMetaObject::invokeMethod(batch, "pause", Qt::QueuedConnection);
    process_until(application, [&] { return paused_seen; });
    QMetaObject::invokeMethod(batch, "removeEntry", Qt::QueuedConnection,
                              Q_ARG(QString, id_a));
    process_until(application, [&] {
        return !id_a.isEmpty() && batch->entryState(id_a) == -1;
    });
    EXPECT_FALSE(finished);
    QMetaObject::invokeMethod(batch, "resume", Qt::QueuedConnection);
    process_until(application, [&] { return finished; });

    EXPECT_EQ(calls.load(), 2);
    EXPECT_EQ(batch->entryState(id_a), -1);
    EXPECT_EQ(batch->entryState(id_b), static_cast<int>(BatchEntryState::Completed));
    EXPECT_FALSE(last_running);
    EXPECT_TRUE(std::filesystem::exists(dir / "b_translated.txt"));
    EXPECT_FALSE(std::filesystem::exists(dir / "a_translated.txt"));

    QMetaObject::invokeMethod(service, &InferenceService::shutdown, Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(context, [&] {
        delete batch;
        delete service;
        context->moveToThread(QCoreApplication::instance()->thread()); }, Qt::BlockingQueuedConnection);
    worker.quit();
    worker.wait();
    delete context;
    std::filesystem::remove_all(dir, ec);
}

TEST(InferenceService, StoreExceptionsAreContainedAtQtBoundaries) {
    int argc = 1;
    char name[] = "batch-store-exception-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(qtrans::core::test::ModelHostHooks{});
    const auto dir = std::filesystem::temp_directory_path() / "qtrans-batch-corrupt-store";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    // A corrupt queue makes BatchStore deserialization throw
    // std::invalid_argument (from std::stoi).
    {
        std::ofstream file(dir / "queue.bq");
        file << "id=corrupt\nfile_type=notanint\n";
    }
    QThread worker;
    worker.start();
    auto *context = new QObject;
    context->moveToThread(&worker);
    InferenceService *service = nullptr;
    BatchController *batch = nullptr;
    QMetaObject::invokeMethod(context, [&] {
        qtrans::core::test::ScopedModelHostHooks worker_hooks(
            qtrans::core::test::ModelHostHooks{});
        service = new InferenceService;
        service->setModelConfig(QStringLiteral("demo"), QString());
        batch = new BatchController(service, (dir / "queue.bq").string(), dir); }, Qt::BlockingQueuedConnection);

    int errors = 0;
    bool loaded = false;
    bool last_running = true;
    QObject::connect(service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    QObject::connect(batch, &BatchController::errorOccurred, &application,
                     [&](const QString &) { ++errors; });
    QObject::connect(batch, &BatchController::batchStateChanged, &application,
                     [&](bool running, bool) { last_running = running; });
    service->loadModel();
    process_until(application, [&] { return loaded; });
    ASSERT_TRUE(loaded);

    // Query boundaries must swallow the store exception and return defaults.
    QStringList ids;
    QMetaObject::invokeMethod(batch, "entryIds", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(QStringList, ids));
    EXPECT_TRUE(ids.isEmpty());
    int state = -2;
    QMetaObject::invokeMethod(batch, "entryState", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(int, state),
                              Q_ARG(QString, QStringLiteral("corrupt")));
    EXPECT_EQ(state, -1);

    // loadPersistedEntries surfaces the failure via errorOccurred.
    QMetaObject::invokeMethod(batch, "loadPersistedEntries", Qt::QueuedConnection);
    process_until(application, [&] { return errors >= 1; });

    // start() must surface the failure and stop cleanly instead of letting
    // the exception escape through the worker event loop.
    QMetaObject::invokeMethod(batch, "start", Qt::QueuedConnection);
    process_until(application, [&] { return errors >= 2; });
    EXPECT_FALSE(last_running);

    QMetaObject::invokeMethod(service, &InferenceService::shutdown, Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(context, [&] {
        delete batch;
        delete service;
        context->moveToThread(QCoreApplication::instance()->thread()); }, Qt::BlockingQueuedConnection);
    worker.quit();
    worker.wait();
    delete context;
    std::filesystem::remove_all(dir, ec);
}

TEST(InferenceService, FailedSegmentNeverCompletesEntryOrWritesOutput) {
    int argc = 1;
    char name[] = "batch-failed-segment-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    std::atomic<int> calls{0};
    qtrans::core::test::ModelHostHooks hooks;
    hooks.generate = [&calls](std::string_view, const qtrans::core::SamplingOptions &,
                              const std::function<void(std::string_view)> &emit_piece,
                              const std::function<bool()> &) {
        const int call = ++calls;
        if (call == 2) {
            return qtrans::core::test::TestGeneration{
                "", 1, 0, false, {qtrans::core::FailureCode::Runtime, "segment failed"}};
        }
        emit_piece("done");
        return qtrans::core::test::TestGeneration{"done", 1, 1, false, {}};
    };
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(hooks);
    const auto dir = std::filesystem::temp_directory_path() / "qtrans-batch-failed-segment";
    const auto input_a = dir / "segments.txt";
    const auto input_b = dir / "b.txt";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    {
        std::ofstream file(input_a);
        file << "first paragraph\n\nsecond paragraph";
    }
    {
        std::ofstream file(input_b);
        file << "second file";
    }
    QThread worker;
    worker.start();
    auto *context = new QObject;
    context->moveToThread(&worker);
    InferenceService *service = nullptr;
    BatchController *batch = nullptr;
    QMetaObject::invokeMethod(context, [&] {
        qtrans::core::test::ScopedModelHostHooks worker_hooks(hooks);
        service = new InferenceService;
        service->setModelConfig(QStringLiteral("demo"), QString());
        batch = new BatchController(service, (dir / "queue.bq").string(), dir); }, Qt::BlockingQueuedConnection);

    QString id_a;
    QString id_b;
    int errors = 0;
    bool loaded = false;
    bool finished = false;
    bool last_running = true;
    QObject::connect(service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    QObject::connect(batch, &BatchController::entryAdded, &application,
                     [&](const QString &id, const QString &, const QString &) {
                         if (id_a.isEmpty())
                             id_a = id;
                         else
                             id_b = id;
                     });
    QObject::connect(batch, &BatchController::batchStateChanged, &application,
                     [&](bool running, bool) { last_running = running; });
    QObject::connect(batch, &BatchController::batchFinished, &application, [&] { finished = true; });
    QObject::connect(batch, &BatchController::errorOccurred, &application,
                     [&](const QString &) { ++errors; });
    service->loadModel();
    process_until(application, [&] { return loaded; });
    ASSERT_TRUE(loaded);

    QMetaObject::invokeMethod(batch, "addFile", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(input_a.string())),
                              Q_ARG(QString, QStringLiteral("Auto")),
                              Q_ARG(QString, QStringLiteral("English")));
    QMetaObject::invokeMethod(batch, "addFile", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(input_b.string())),
                              Q_ARG(QString, QStringLiteral("Auto")),
                              Q_ARG(QString, QStringLiteral("English")));
    QMetaObject::invokeMethod(batch, "start", Qt::QueuedConnection);

    // First run: the second segment of entry A fails, so A must land in Failed,
    // the batch stops, and no output file is written for either entry.
    process_until(application, [&] { return errors >= 1; });
    EXPECT_EQ(batch->entryState(id_a), static_cast<int>(BatchEntryState::Failed));
    EXPECT_EQ(batch->entryState(id_b), static_cast<int>(BatchEntryState::Queued));
    EXPECT_FALSE(last_running);
    EXPECT_FALSE(std::filesystem::exists(dir / "segments_translated.txt"));
    EXPECT_FALSE(std::filesystem::exists(dir / "b_translated.txt"));

    // Restart: entry A's remaining Pending segments must not execute because a
    // segment already failed; the queue skips A and completes entry B instead.
    QMetaObject::invokeMethod(batch, "start", Qt::QueuedConnection);
    process_until(application, [&] { return finished; });
    EXPECT_EQ(batch->entryState(id_a), static_cast<int>(BatchEntryState::Failed));
    EXPECT_EQ(batch->entryState(id_b), static_cast<int>(BatchEntryState::Completed));
    EXPECT_EQ(calls.load(), 3);
    EXPECT_FALSE(std::filesystem::exists(dir / "segments_translated.txt"));
    EXPECT_TRUE(std::filesystem::exists(dir / "b_translated.txt"));

    QMetaObject::invokeMethod(service, &InferenceService::shutdown, Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(context, [&] {
        delete batch;
        delete service;
        context->moveToThread(QCoreApplication::instance()->thread()); }, Qt::BlockingQueuedConnection);
    worker.quit();
    worker.wait();
    delete context;
    std::filesystem::remove_all(dir, ec);
}

TEST(InferenceService, ConcurrentPopupMainAndBatchJobsCorrelateIndependently) {
    int argc = 1;
    char name[] = "inference-correlation-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(qtrans::core::test::ModelHostHooks{});
    InferenceService service;
    service.setModelConfig(QStringLiteral("demo"), QString());
    bool loaded = false;
    QObject::connect(&service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    service.loadModel();
    process_until(application, [&] { return loaded; });
    ASSERT_TRUE(loaded);

    auto popup_request = native_request();
    popup_request.source = "popup selection";
    popup_request.wordselect = true;
    auto main_request = native_request();
    main_request.source = "main window text";
    BatchTranslationRequest batch_request;
    batch_request.source = "batch segment text";
    batch_request.target_language = "English";
    batch_request.source_language = "Auto";

    const TranslationJobId popup_job = service.translateNative(popup_request);
    const TranslationJobId main_job = service.translateNative(main_request);
    const TranslationJobId batch_job = service.translateBatch(batch_request);

    // Each consumer's active id is its synchronously returned job id and is
    // never reassigned from a translationStarted event.
    ConsumerLog popup_log{popup_job, popup_job};
    ConsumerLog main_log{main_job, main_job};
    ConsumerLog batch_log{batch_job, batch_job};
    watch_job(service, application, popup_log);
    watch_job(service, application, main_log);
    watch_job(service, application, batch_log);

    process_until(application, [&] {
        return popup_log.finished.size() == 1 && main_log.finished.size() == 1 &&
               batch_log.finished.size() == 1;
    });

    // All consumers were exposed to unrelated started events and ignored them
    // without changing their active id.
    EXPECT_TRUE(popup_log.foreign_started_seen);
    EXPECT_TRUE(main_log.foreign_started_seen);
    EXPECT_TRUE(batch_log.foreign_started_seen);
    EXPECT_EQ(popup_log.active, popup_job);
    EXPECT_EQ(main_log.active, main_job);
    EXPECT_EQ(batch_log.active, batch_job);

    EXPECT_EQ(popup_log.started, std::vector<TranslationJobId>{popup_job});
    EXPECT_EQ(main_log.started, std::vector<TranslationJobId>{main_job});
    EXPECT_EQ(batch_log.started, std::vector<TranslationJobId>{batch_job});
    EXPECT_GE(popup_log.deltas.size(), 1U);
    EXPECT_GE(main_log.deltas.size(), 1U);
    EXPECT_GE(batch_log.deltas.size(), 1U);
    EXPECT_EQ(popup_log.finished, std::vector<TranslationJobId>{popup_job});
    EXPECT_EQ(main_log.finished, std::vector<TranslationJobId>{main_job});
    EXPECT_EQ(batch_log.finished, std::vector<TranslationJobId>{batch_job});
    EXPECT_EQ(service.jobState(popup_job), TranslationState::Completed);
    EXPECT_EQ(service.jobState(main_job), TranslationState::Completed);
    EXPECT_EQ(service.jobState(batch_job), TranslationState::Completed);
    service.shutdown();
}
