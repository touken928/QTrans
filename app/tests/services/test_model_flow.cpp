#include "ui/shell/model_flow.h"

#include "download/download_service.h"
#include "translate/inference_service.h"
#include "model_host_test_access.h"

#include <gtest/gtest.h>

#include <QCoreApplication>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

namespace {

void process_until(QCoreApplication &application, const std::function<bool()> &condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition() && std::chrono::steady_clock::now() < deadline) {
        application.processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::yield();
    }
}

// Completes immediately and records the load count observed at the moment its
// download() returns; the correlated load must not have started yet.
class CompletingDownloader final : public IModelDownloader {
public:
    std::atomic<int> *load_count = nullptr;
    std::atomic<int> loads_at_return{-1};

    ExecutionResult download(const DownloadRequest &, const DownloadCancelToken *,
                             DownloadProgressHandler progress) override {
        if (progress) progress({10, 20, 5.0, 2.0});
        loads_at_return.store(load_count != nullptr ? load_count->load() : -1);
        return {ExecutionOutcome::Completed, {}};
    }
};

// Runs until cancelled; used to hold a download open for stale/cancel tests.
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

}  // namespace

TEST(ModelFlow, CompletingDownloadLoadsAfterFinish) {
    int argc = 1;
    char name[] = "model-flow-complete-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);

    std::atomic<int> load_calls{0};
    qtrans::core::test::ModelHostHooks hooks;
    hooks.load_runtime = [&load_calls](const qtrans::core::ModelSpec &) {
        ++load_calls;
        return qtrans::core::Failure{};
    };
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(hooks);

    auto downloader = std::make_unique<CompletingDownloader>();
    downloader->load_count = &load_calls;
    CompletingDownloader *downloader_ptr = downloader.get();
    DownloadService download_service(std::move(downloader));
    InferenceService inference_service;
    inference_service.setModelConfig(QStringLiteral("m"), QString());

    ModelFlow flow(&inference_service, &download_service);
    bool dialog_shown = false;
    QObject::connect(&flow, &ModelFlow::showDownloadDialog, &application,
                     [&dialog_shown]() { dialog_shown = true; });

    flow.beginDownloadThenLoad(QStringLiteral("m"));
    // The synchronous reservation must have bound the download before events
    // are drained, and the dialog must have been requested.
    EXPECT_TRUE(flow.downloadActive());
    EXPECT_TRUE(dialog_shown);

    process_until(application, [&] { return load_calls.load() == 1; });

    EXPECT_EQ(load_calls.load(), 1);
    // The download completed before the load was dispatched.
    EXPECT_EQ(downloader_ptr->loads_at_return.load(), 0);
    EXPECT_TRUE(dialog_shown);
    download_service.shutdown();
    inference_service.shutdown();
}

TEST(ModelFlow, StaleFinishIsIgnored) {
    int argc = 1;
    char name[] = "model-flow-stale-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);

    std::atomic<int> load_calls{0};
    qtrans::core::test::ModelHostHooks hooks;
    hooks.load_runtime = [&load_calls](const qtrans::core::ModelSpec &) {
        ++load_calls;
        return qtrans::core::Failure{};
    };
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(hooks);

    DownloadService download_service(std::make_unique<BlockingDownloader>());
    InferenceService inference_service;
    inference_service.setModelConfig(QStringLiteral("m"), QString());

    ModelFlow flow(&inference_service, &download_service);
    flow.beginDownloadThenLoad(QStringLiteral("m"));
    const DownloadId bound = flow.activeDownloadId();
    ASSERT_TRUE(bound.is_valid());

    DownloadResult stale;
    stale.id = DownloadId{bound.value + 100};
    stale.state = DownloadState::Completed;
    flow.onDownloadFinished(stale);

    EXPECT_EQ(load_calls.load(), 0);
    EXPECT_EQ(flow.activeDownloadId(), bound);
    EXPECT_TRUE(flow.downloadActive());
    download_service.shutdown();
    inference_service.shutdown();
}

TEST(ModelFlow, CancelFromDialogPreventsLoad) {
    int argc = 1;
    char name[] = "model-flow-cancel-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);

    std::atomic<int> load_calls{0};
    qtrans::core::test::ModelHostHooks hooks;
    hooks.load_runtime = [&load_calls](const qtrans::core::ModelSpec &) {
        ++load_calls;
        return qtrans::core::Failure{};
    };
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(hooks);

    auto downloader = std::make_unique<BlockingDownloader>();
    BlockingDownloader *downloader_ptr = downloader.get();
    DownloadService download_service(std::move(downloader));
    InferenceService inference_service;
    inference_service.setModelConfig(QStringLiteral("m"), QString());

    ModelFlow flow(&inference_service, &download_service);
    flow.beginDownloadThenLoad(QStringLiteral("m"));
    process_until(application, [&] { return downloader_ptr->started.load(); });
    ASSERT_TRUE(downloader_ptr->started.load());

    flow.cancelFromDialog();
    process_until(application, [&] { return !flow.downloadActive(); });

    EXPECT_FALSE(flow.downloadActive());
    EXPECT_EQ(load_calls.load(), 0);
    download_service.shutdown();
    inference_service.shutdown();
}

TEST(ModelFlow, UnloadFailureClearsBusyState) {
    int argc = 1;
    char name[] = "model-flow-unload-failure-test";
    char *argv[] = {name, nullptr};
    QCoreApplication application(argc, argv);

    std::atomic<int> unload_calls{0};
    qtrans::core::test::ModelHostHooks hooks;
    hooks.unload_runtime = [&unload_calls]() {
        ++unload_calls;
        return qtrans::core::Failure{qtrans::core::FailureCode::Runtime, "unload failed"};
    };
    qtrans::core::test::ScopedModelHostHooks scoped_hooks(hooks);

    DownloadService download_service(std::make_unique<CompletingDownloader>());
    InferenceService inference_service;
    inference_service.setModelConfig(QStringLiteral("m"), QString());

    bool loaded = false;
    QObject::connect(&inference_service, &InferenceService::modelLoadFinished, &application,
                     [&loaded](bool success, const QString &, const QString &) {
                         loaded = success;
                     });
    inference_service.loadModel();
    process_until(application, [&] { return loaded; });
    ASSERT_TRUE(loaded);

    ModelFlow flow(&inference_service, &download_service);
    bool unload_success = true;
    QObject::connect(&flow, &ModelFlow::unloadFinished, &application,
                     [&unload_success](bool success, const QString &) { unload_success = success; });

    flow.beginUnload();
    process_until(application, [&] { return !flow.unloading(); });

    EXPECT_FALSE(flow.unloading());
    EXPECT_EQ(unload_calls.load(), 1);
    EXPECT_FALSE(unload_success);
    download_service.shutdown();
    inference_service.shutdown();
}
