#include "translate/inference_service.h"
#include "batch/batch_controller.h"
#include "model_host_test_access.h"

#include <gtest/gtest.h>

namespace inference_service_test {
void set_before_core_submit_hook(std::function<void(TranslationJobId)> hook);
}

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

// Standard queue file name inside a scratch batch directory.
std::filesystem::path queue_file(const std::filesystem::path &dir) {
    return dir / "queue.bq";
}

// Find a projected entry by id in a queueSnapshot payload.
const BatchEntryView *find_view(const QVector<BatchEntryView> &snapshot,
                                const QString &id) {
    for (const auto &view : snapshot) {
        if (view.id == id) return &view;
    }
    return nullptr;
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
    const TranslationJobTicket ticket = service.submitNative(native_request());
    const TranslationJobId id = ticket.id;
    EXPECT_TRUE(id.is_valid());
    process_until(application, [&] { return translated; });
    EXPECT_TRUE(translated);
    EXPECT_EQ(service.jobState(id), TranslationState::Completed);
    const RuntimeSnapshot snapshot = service.runtimeSnapshot();
    EXPECT_EQ(snapshot.lifecycle, qtrans::core::LifecycleState::Ready);
    EXPECT_EQ(snapshot.loaded_model_id, "demo");
    EXPECT_EQ(snapshot.active_translation_jobs, 0U);
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

TEST(InferenceService, CancellationRacingCoreHandleInstallationIsReplayed) {
    int argc = 1;
    char name[] = "inference-service-submit-cancel-race-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    std::atomic<int> generation_calls{0};
    qtrans::core::test::ModelHostHooks hooks;
    hooks.generate = [&generation_calls](std::string_view,
                                         const qtrans::core::SamplingOptions &,
                                         const std::function<void(std::string_view)> &,
                                         const std::function<bool()> &) {
        ++generation_calls;
        return qtrans::core::test::TestGeneration{"unexpected", 1, 1, false, {}};
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

    inference_service_test::set_before_core_submit_hook(
        [&](TranslationJobId id) { EXPECT_TRUE(service.cancel(id)); });
    bool cancelled = false;
    QObject::connect(&service, &InferenceService::translationFinished, &application,
                     [&](const TranslationJobResult &result) {
                         cancelled = result.state == TranslationState::Cancelled;
                     });
    const TranslationJobId id = service.translateNative(native_request());
    inference_service_test::set_before_core_submit_hook({});

    process_until(application, [&] { return cancelled; });
    EXPECT_TRUE(cancelled);
    EXPECT_EQ(service.jobState(id), TranslationState::Cancelled);
    EXPECT_EQ(generation_calls.load(), 0);
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
    const qtrans::core::BackendState state = service.initializeBackend();
    EXPECT_FALSE(state.label.empty());
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

    QString removal_id;
    QVector<BatchEntryView> snapshot;
    bool loaded = false;
    bool finished = false;
    bool removed = false;
    bool last_running = true;
    QObject::connect(service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    QObject::connect(batch, &BatchController::queueSnapshot, &application,
                     [&](const QVector<BatchEntryView> &entries) {
                         snapshot = entries;
                         if (!removal_id.isEmpty() && !find_view(snapshot, removal_id))
                             removed = true;
                     });
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
                         if (removal_requested || snapshot.isEmpty()) return;
                         removal_requested = true;
                         removal_id = snapshot.front().id;
                         QMetaObject::invokeMethod(batch, "removeEntry", Qt::QueuedConnection,
                                                   Q_ARG(QString, removal_id));
                     });
    QMetaObject::invokeMethod(batch, "start", Qt::QueuedConnection);
    process_until(application, [&] { return finished; });

    EXPECT_TRUE(removed);
    // First entry was aborted mid-run (call 1); second entry ran to completion.
    EXPECT_EQ(calls.load(), 2);
    ASSERT_EQ(snapshot.size(), 1);
    EXPECT_EQ(snapshot.front().state, static_cast<int>(BatchEntryState::Completed));
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

    QString removal_id;
    QVector<BatchEntryView> snapshot;
    bool loaded = false;
    bool finished = false;
    bool removal_requested = false;
    QObject::connect(service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    QObject::connect(batch, &BatchController::queueSnapshot, &application,
                     [&](const QVector<BatchEntryView> &entries) { snapshot = entries; });
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
                         if (removal_requested || snapshot.isEmpty()) return;
                         removal_requested = true;
                         removal_id = snapshot.front().id;
                         QMetaObject::invokeMethod(batch, "removeEntry", Qt::QueuedConnection,
                                                   Q_ARG(QString, removal_id));
                     });
    QMetaObject::invokeMethod(batch, "start", Qt::QueuedConnection);

    // Wait until removeEntry was processed (entry A is gone from the snapshot).
    process_until(application, [&] {
        return !removal_id.isEmpty() && find_view(snapshot, removal_id) == nullptr;
    });

    // The old job is still blocked on the gate, so its terminal event has not
    // been observed yet: the next entry must not have been submitted.
    ASSERT_EQ(snapshot.size(), 1);
    EXPECT_EQ(snapshot.front().state, static_cast<int>(BatchEntryState::Queued));

    // Release the old job; its terminal is consumed and only then does the
    // queue advance to entry B.
    release_first = true;
    process_until(application, [&] {
        return finished && snapshot.size() == 1 &&
               snapshot.front().state == static_cast<int>(BatchEntryState::Completed);
    });
    EXPECT_EQ(calls.load(), 2);
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

    QString removal_id;
    QVector<BatchEntryView> snapshot;
    bool loaded = false;
    bool started = false;
    bool paused_seen = false;
    bool finished = false;
    bool last_running = true;
    QObject::connect(service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    QObject::connect(batch, &BatchController::queueSnapshot, &application,
                     [&](const QVector<BatchEntryView> &entries) { snapshot = entries; });
    QObject::connect(service, &InferenceService::translationStarted, &application,
                     [&](TranslationJobId) {
                         started = true;
                         if (removal_id.isEmpty()) {
                             for (const auto &view : snapshot) {
                                 if (view.state == static_cast<int>(BatchEntryState::Processing)) {
                                     removal_id = view.id;
                                     break;
                                 }
                             }
                         }
                     });
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
                              Q_ARG(QString, removal_id));
    process_until(application, [&] {
        return !removal_id.isEmpty() && find_view(snapshot, removal_id) == nullptr;
    });
    EXPECT_FALSE(finished);
    QMetaObject::invokeMethod(batch, "resume", Qt::QueuedConnection);
    process_until(application, [&] {
        return finished && snapshot.size() == 1 &&
               snapshot.front().state == static_cast<int>(BatchEntryState::Completed);
    });

    EXPECT_EQ(calls.load(), 2);
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

TEST(InferenceService, CorruptStoreIsQuarantinedAtStartup) {
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

    QVector<BatchEntryView> snapshot;
    bool snapshot_seen = false;
    int errors = 0;
    bool loaded = false;
    bool last_running = true;
    QObject::connect(service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    QObject::connect(batch, &BatchController::queueSnapshot, &application,
                     [&](const QVector<BatchEntryView> &entries) {
                         snapshot = entries;
                         snapshot_seen = true;
                     });
    QObject::connect(batch, &BatchController::errorOccurred, &application,
                     [&](const QString &) { ++errors; });
    QObject::connect(batch, &BatchController::batchStateChanged, &application,
                     [&](bool running, bool) { last_running = running; });
    service->loadModel();
    process_until(application, [&] { return loaded; });
    ASSERT_TRUE(loaded);

    // loadPersistedEntries surfaces the failure via errorOccurred after
    // quarantining the corrupt queue, then projects the now-empty queue.
    QMetaObject::invokeMethod(batch, "loadPersistedEntries", Qt::QueuedConnection);
    process_until(application, [&] { return errors >= 1 && snapshot_seen; });
    EXPECT_TRUE(snapshot.isEmpty());

    EXPECT_FALSE(std::filesystem::exists(dir / "queue.bq"));
    bool found_quarantine = false;
    for (const auto &item : std::filesystem::directory_iterator(dir)) {
        if (item.path().filename().string().find("queue.bq.corrupt.") == 0) {
            found_quarantine = true;
        }
    }
    EXPECT_TRUE(found_quarantine);

    // The quarantined queue is no longer allowed to poison later runs.
    QMetaObject::invokeMethod(batch, "start", Qt::QueuedConnection);
    process_until(application, [&] { return !last_running; });
    EXPECT_EQ(errors, 1);

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
    QVector<BatchEntryView> snapshot;
    int errors = 0;
    bool loaded = false;
    bool finished = false;
    bool last_running = true;
    QObject::connect(service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) { loaded = success; });
    QObject::connect(batch, &BatchController::queueSnapshot, &application,
                     [&](const QVector<BatchEntryView> &entries) {
                         snapshot = entries;
                         if (entries.size() >= 1 && id_a.isEmpty())
                             id_a = entries.at(0).id;
                         if (entries.size() >= 2 && id_b.isEmpty())
                             id_b = entries.at(1).id;
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
    process_until(application, [&] {
        const auto *a = find_view(snapshot, id_a);
        return a && a->state == static_cast<int>(BatchEntryState::Failed) && !last_running;
    });
    const auto *a_view = find_view(snapshot, id_a);
    const auto *b_view = find_view(snapshot, id_b);
    ASSERT_NE(a_view, nullptr);
    ASSERT_NE(b_view, nullptr);
    EXPECT_EQ(a_view->state, static_cast<int>(BatchEntryState::Failed));
    EXPECT_EQ(b_view->state, static_cast<int>(BatchEntryState::Queued));
    EXPECT_GE(errors, 1);
    EXPECT_FALSE(last_running);
    EXPECT_FALSE(std::filesystem::exists(dir / "segments_translated.txt"));
    EXPECT_FALSE(std::filesystem::exists(dir / "b_translated.txt"));

    // Restart: entry A's remaining Pending segments must not execute because a
    // segment already failed; the queue skips A and completes entry B instead.
    QMetaObject::invokeMethod(batch, "start", Qt::QueuedConnection);
    process_until(application, [&] {
        const auto *b = find_view(snapshot, id_b);
        return finished && b && b->state == static_cast<int>(BatchEntryState::Completed);
    });
    const auto *a_final = find_view(snapshot, id_a);
    const auto *b_final = find_view(snapshot, id_b);
    ASSERT_NE(a_final, nullptr);
    ASSERT_NE(b_final, nullptr);
    EXPECT_EQ(a_final->state, static_cast<int>(BatchEntryState::Failed));
    EXPECT_EQ(b_final->state, static_cast<int>(BatchEntryState::Completed));
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

TEST(InferenceService, OutputFailureNeverPublishesCompletedOrSaved) {
    int argc = 1;
    char name[] = "batch-output-failure-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(
        qtrans::core::test::ModelHostHooks{});
    const auto dir = std::filesystem::temp_directory_path() /
                     "qtrans-batch-output-failure";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    const auto input = dir / "input.txt";
    const auto output_blocker = dir / "not-a-directory";
    {
        std::ofstream file(input);
        file << "hello";
    }
    {
        std::ofstream file(output_blocker);
        file << "block";
    }

    InferenceService service;
    service.setModelConfig(QStringLiteral("demo"), QString());
    BatchController batch(&service, dir / "queue.bq", output_blocker);
    QVector<BatchEntryView> snapshot;
    bool loaded = false;
    int errors = 0;
    QObject::connect(&service, &InferenceService::modelLoadFinished, &application,
                     [&](bool success, const QString &, const QString &) {
                         loaded = success;
                     });
    QObject::connect(&batch, &BatchController::queueSnapshot, &application,
                     [&](const QVector<BatchEntryView> &entries) { snapshot = entries; });
    QObject::connect(&batch, &BatchController::errorOccurred, &application,
                     [&](const QString &) { ++errors; });
    service.loadModel();
    process_until(application, [&] { return loaded; });
    ASSERT_TRUE(loaded);

    batch.addFile(QString::fromStdString(input.u8string()), QStringLiteral("Auto"),
                  QStringLiteral("Chinese"));
    ASSERT_EQ(snapshot.size(), 1);
    const QString id = snapshot.front().id;
    ASSERT_FALSE(id.isEmpty());
    batch.start();
    process_until(application, [&] {
        const auto *view = find_view(snapshot, id);
        return view && view->state == static_cast<int>(BatchEntryState::Failed);
    });
    const auto *view = find_view(snapshot, id);
    ASSERT_NE(view, nullptr);
    EXPECT_EQ(view->state, static_cast<int>(BatchEntryState::Failed));
    EXPECT_FALSE(view->completed);
    EXPECT_FALSE(view->saved);
    EXPECT_TRUE(view->save_path.isEmpty());
    EXPECT_GE(errors, 1);
    service.shutdown();
    std::filesystem::remove_all(dir);
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

TEST(InferenceService, BatchControllerRestoresPersistedQueueAsSnapshot) {
    int argc = 1;
    char name[] = "batch-snapshot-restore-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    const auto dir = std::filesystem::temp_directory_path() / "qtrans-batch-snapshot-restore";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    // Seed the durable queue before the controller starts, as a restart
    // would leave it.
    BatchEntry entry;
    entry.id = "persisted-1";
    entry.file.path = dir / "persisted.txt";
    entry.file.file_type = BatchFileType::Txt;
    entry.source_language = "Auto";
    entry.target_language = "English";
    entry.state = BatchEntryState::Queued;
    entry.created_at = 1;
    entry.updated_at = 2;
    BatchSegment seg;
    seg.index = 0;
    seg.start_line = 0;
    seg.end_line = 2;
    seg.source_text = "persisted content";
    entry.file.segments = {seg};
    BatchStore(queue_file(dir)).append(entry);

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
        batch = new BatchController(service, queue_file(dir).string(), dir); }, Qt::BlockingQueuedConnection);

    QVector<BatchEntryView> snapshot;
    QObject::connect(batch, &BatchController::queueSnapshot, &application,
                     [&](const QVector<BatchEntryView> &entries) { snapshot = entries; });
    QMetaObject::invokeMethod(batch, "loadPersistedEntries", Qt::QueuedConnection);
    process_until(application, [&] { return snapshot.size() == 1; });

    ASSERT_EQ(snapshot.size(), 1);
    const BatchEntryView first = snapshot.front();
    EXPECT_EQ(first.id, QStringLiteral("persisted-1"));
    EXPECT_EQ(first.file, QStringLiteral("persisted.txt"));
    EXPECT_EQ(first.file_path,
              QString::fromStdString((dir / "persisted.txt").string()));
    EXPECT_EQ(first.source, QStringLiteral("Auto"));
    EXPECT_EQ(first.target, QStringLiteral("English"));
    EXPECT_EQ(first.state, static_cast<int>(BatchEntryState::Queued));
    EXPECT_EQ(first.segments_done, 0);
    EXPECT_EQ(first.segments_total, 1);
    EXPECT_FALSE(first.saved);

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

TEST(InferenceService, BatchControllerEmitsSnapshotForMutations) {
    int argc = 1;
    char name[] = "batch-snapshot-mutation-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(qtrans::core::test::ModelHostHooks{});
    const auto dir = std::filesystem::temp_directory_path() / "qtrans-batch-snapshot-mutation";
    const auto input = dir / "input.txt";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    {
        std::ofstream file(input);
        file << "file content";
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
        batch = new BatchController(service, (dir / "queue.bq").string(), dir); }, Qt::BlockingQueuedConnection);

    QVector<BatchEntryView> snapshot;
    QObject::connect(batch, &BatchController::queueSnapshot, &application,
                     [&](const QVector<BatchEntryView> &entries) { snapshot = entries; });

    // Add: one complete snapshot with the new entry.
    QMetaObject::invokeMethod(batch, "addFile", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString(input.string())),
                              Q_ARG(QString, QStringLiteral("Auto")),
                              Q_ARG(QString, QStringLiteral("English")));
    process_until(application, [&] { return snapshot.size() == 1; });
    ASSERT_EQ(snapshot.size(), 1);
    const QString id = snapshot.front().id;
    EXPECT_FALSE(id.isEmpty());
    EXPECT_EQ(snapshot.front().file, QStringLiteral("input.txt"));
    EXPECT_EQ(snapshot.front().source, QStringLiteral("Auto"));
    EXPECT_EQ(snapshot.front().target, QStringLiteral("English"));
    EXPECT_EQ(snapshot.front().state,
              static_cast<int>(BatchEntryState::Queued));

    // Remove: the snapshot empties again.
    QMetaObject::invokeMethod(batch, "removeEntry", Qt::QueuedConnection,
                              Q_ARG(QString, id));
    process_until(application, [&] { return snapshot.isEmpty(); });
    EXPECT_TRUE(snapshot.isEmpty());

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

TEST(InferenceService, BatchControllerRetryResetsFailedEntryOnly) {
    int argc = 1;
    char name[] = "batch-retry-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    const auto dir = std::filesystem::temp_directory_path() / "qtrans-batch-retry";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    // Seed one failed entry (one completed segment, one failed segment) and
    // one completed entry; only the failed one may transition back.
    BatchEntry failed;
    failed.id = "failed-1";
    failed.file.path = dir / "failed.txt";
    failed.file.file_type = BatchFileType::Txt;
    failed.source_language = "Auto";
    failed.target_language = "English";
    failed.state = BatchEntryState::Failed;
    failed.created_at = 1;
    failed.updated_at = 2;
    BatchSegment done;
    done.index = 0;
    done.state = BatchSegmentState::Completed;
    done.translated_text = "translated";
    BatchSegment broken;
    broken.index = 1;
    broken.state = BatchSegmentState::Failed;
    failed.file.segments = {done, broken};

    BatchEntry finished;
    finished.id = "completed-1";
    finished.file.path = dir / "finished.txt";
    finished.file.file_type = BatchFileType::Txt;
    finished.source_language = "Auto";
    finished.target_language = "English";
    finished.state = BatchEntryState::Completed;
    finished.created_at = 3;
    finished.updated_at = 4;
    BatchSegment all_done;
    all_done.index = 0;
    all_done.state = BatchSegmentState::Completed;
    finished.file.segments = {all_done};

    BatchStore(queue_file(dir)).save({failed, finished});

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
        batch = new BatchController(service, queue_file(dir).string(), dir); }, Qt::BlockingQueuedConnection);

    QVector<BatchEntryView> snapshot;
    int retry_errors = 0;
    QObject::connect(batch, &BatchController::queueSnapshot, &application,
                     [&](const QVector<BatchEntryView> &entries) { snapshot = entries; });
    QObject::connect(batch, &BatchController::errorOccurred, &application,
                     [&](const QString &) { ++retry_errors; });

    // Seed the projection from the persisted queue.
    QMetaObject::invokeMethod(batch, "loadPersistedEntries", Qt::QueuedConnection);
    process_until(application, [&] { return snapshot.size() == 2; });
    ASSERT_EQ(snapshot.size(), 2);
    const auto *completed_view = find_view(snapshot, QStringLiteral("completed-1"));
    ASSERT_NE(completed_view, nullptr);
    EXPECT_EQ(completed_view->state, static_cast<int>(BatchEntryState::Completed));

    // Retrying a completed entry is rejected and leaves it untouched.
    QMetaObject::invokeMethod(batch, "retryEntry", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("completed-1")));
    process_until(application, [&] { return retry_errors == 1; });
    completed_view = find_view(snapshot, QStringLiteral("completed-1"));
    ASSERT_NE(completed_view, nullptr);
    EXPECT_EQ(completed_view->state, static_cast<int>(BatchEntryState::Completed));

    // Retrying the failed entry resets it: state back to Queued, failed
    // segments Pending again, completed work preserved.
    QMetaObject::invokeMethod(batch, "retryEntry", Qt::QueuedConnection,
                              Q_ARG(QString, QStringLiteral("failed-1")));
    process_until(application, [&] {
        const auto *failed = find_view(snapshot, QStringLiteral("failed-1"));
        return failed && failed->state == static_cast<int>(BatchEntryState::Queued);
    });
    const auto *reset_view = find_view(snapshot, QStringLiteral("failed-1"));
    ASSERT_NE(reset_view, nullptr);
    EXPECT_EQ(reset_view->state, static_cast<int>(BatchEntryState::Queued));
    // Completed segment progress is preserved in the projection.
    EXPECT_EQ(reset_view->segments_done, 1);
    EXPECT_EQ(reset_view->segments_total, 2);

    const auto persisted = BatchStore(queue_file(dir)).load();
    ASSERT_EQ(persisted.size(), 2u);
    const BatchEntry *reset = nullptr;
    for (const auto &e : persisted) {
        if (e.id == "failed-1") reset = &e;
    }
    ASSERT_NE(reset, nullptr);
    EXPECT_EQ(reset->state, BatchEntryState::Queued);
    ASSERT_EQ(reset->file.segments.size(), 2u);
    EXPECT_EQ(reset->file.segments[0].state, BatchSegmentState::Completed);
    EXPECT_EQ(reset->file.segments[0].translated_text, "translated");
    EXPECT_EQ(reset->file.segments[1].state, BatchSegmentState::Pending);

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

TEST(InferenceService, BatchControllerRecoveryNormalizesProcessingAndRepairsDuplicates) {
    int argc = 1;
    char name[] = "batch-recovery-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    const auto dir = std::filesystem::temp_directory_path() / "qtrans-batch-recovery";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    // Seed one abandoned Processing entry (with a completed segment
    // checkpoint) and two entries sharing one id, as an interrupted
    // pre-collision-era queue would leave them.
    BatchEntry processing;
    processing.id = "interrupted-1";
    processing.file.path = dir / "interrupted.txt";
    processing.file.file_type = BatchFileType::Txt;
    processing.source_language = "Auto";
    processing.target_language = "English";
    processing.state = BatchEntryState::Processing;
    processing.created_at = 1;
    processing.updated_at = 2;
    BatchSegment checkpoint;
    checkpoint.index = 0;
    checkpoint.state = BatchSegmentState::Completed;
    checkpoint.translated_text = "checkpoint";
    BatchSegment pending;
    pending.index = 1;
    pending.state = BatchSegmentState::Pending;
    processing.file.segments = {checkpoint, pending};

    BatchEntry duplicate_a;
    duplicate_a.id = "dup-id";
    duplicate_a.file.path = dir / "dup-a.txt";
    duplicate_a.file.file_type = BatchFileType::Txt;
    duplicate_a.source_language = "Auto";
    duplicate_a.target_language = "English";
    duplicate_a.state = BatchEntryState::Queued;
    BatchEntry duplicate_b = duplicate_a;
    duplicate_b.file.path = dir / "dup-b.txt";

    BatchStore(queue_file(dir)).save({processing, duplicate_a, duplicate_b});

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
        batch = new BatchController(service, queue_file(dir).string(), dir); }, Qt::BlockingQueuedConnection);

    QVector<BatchEntryView> snapshot;
    QObject::connect(batch, &BatchController::queueSnapshot, &application,
                     [&](const QVector<BatchEntryView> &entries) { snapshot = entries; });
    QMetaObject::invokeMethod(batch, "loadPersistedEntries", Qt::QueuedConnection);
    process_until(application, [&] { return snapshot.size() == 3; });

    ASSERT_EQ(snapshot.size(), 3);
    // The interrupted entry normalized back to Queued with its checkpoint.
    const BatchEntryView first = snapshot.at(0);
    EXPECT_EQ(first.id, QStringLiteral("interrupted-1"));
    EXPECT_EQ(first.state, static_cast<int>(BatchEntryState::Queued));
    EXPECT_EQ(first.segments_done, 1);
    EXPECT_EQ(first.segments_total, 2);
    // The duplicate ids were repaired: every projected id is unique.
    const QString id_b = snapshot.at(1).id;
    const QString id_c = snapshot.at(2).id;
    EXPECT_FALSE(id_b.isEmpty());
    EXPECT_FALSE(id_c.isEmpty());
    EXPECT_NE(id_b, id_c);

    // The repair is persisted: a fresh load sees unique ids and no
    // Processing state.
    const auto persisted = BatchStore(queue_file(dir)).load();
    ASSERT_EQ(persisted.size(), 3u);
    EXPECT_EQ(persisted[0].state, BatchEntryState::Queued);
    EXPECT_NE(persisted[1].id, persisted[2].id);

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

TEST(InferenceService, BatchControllerIdsDoNotCollideForSameStem) {
    int argc = 1;
    char name[] = "batch-id-collision-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(qtrans::core::test::ModelHostHooks{});
    const auto dir = std::filesystem::temp_directory_path() / "qtrans-batch-id-collision";
    const auto dir_a = dir / "a";
    const auto dir_b = dir / "b";
    std::error_code ec;
    std::filesystem::create_directories(dir_a, ec);
    std::filesystem::create_directories(dir_b, ec);
    // Same stem in two folders: the old second-resolution ids would collide.
    {
        std::ofstream file(dir_a / "report.txt");
        file << "first report";
    }
    {
        std::ofstream file(dir_b / "report.txt");
        file << "second report";
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
        batch = new BatchController(service, (dir / "queue.bq").string(), dir); }, Qt::BlockingQueuedConnection);

    QVector<BatchEntryView> snapshot;
    QObject::connect(batch, &BatchController::queueSnapshot, &application,
                     [&](const QVector<BatchEntryView> &entries) { snapshot = entries; });

    QMetaObject::invokeMethod(batch, "addFile", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString((dir_a / "report.txt").string())),
                              Q_ARG(QString, QStringLiteral("Auto")),
                              Q_ARG(QString, QStringLiteral("English")));
    QMetaObject::invokeMethod(batch, "addFile", Qt::QueuedConnection,
                              Q_ARG(QString, QString::fromStdString((dir_b / "report.txt").string())),
                              Q_ARG(QString, QStringLiteral("Auto")),
                              Q_ARG(QString, QStringLiteral("English")));
    process_until(application, [&] { return snapshot.size() == 2; });

    ASSERT_EQ(snapshot.size(), 2);
    const QString first_id = snapshot.at(0).id;
    const QString second_id = snapshot.at(1).id;
    EXPECT_FALSE(first_id.isEmpty());
    EXPECT_FALSE(second_id.isEmpty());
    EXPECT_NE(first_id, second_id) << "same-stem enqueues must never share an id";

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

TEST(InferenceService, FailedUnloadEmitsTerminalResult) {
    int argc = 1;
    char name[] = "inference-service-unload-failure-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);
    qtrans::core::test::ModelHostHooks hooks;
    hooks.unload_runtime = [] {
        return qtrans::core::Failure{qtrans::core::FailureCode::Runtime,
                                     "runtime refused to unload"};
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

    // The unload attempt fails, but a terminal result must still arrive so
    // consumers can clear their lifecycle busy state.
    bool terminal_received = false;
    bool reported_success = true;
    QString error_message;
    QObject::connect(&service, &InferenceService::modelUnloadFinished, &application,
                     [&](bool success, const QString &message) {
                         terminal_received = true;
                         reported_success = success;
                         error_message = message;
                     });
    service.unloadModel();
    process_until(application, [&] { return terminal_received; });
    EXPECT_TRUE(terminal_received);
    EXPECT_FALSE(reported_success);
    EXPECT_FALSE(error_message.isEmpty());
    service.shutdown();
}
