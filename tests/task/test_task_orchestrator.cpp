#include "domain/tasks/task_orchestrator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
class FakeDownloader final : public IModelDownloader {
public:
    ExecutionResult result{ExecutionOutcome::Completed, {}};
    ExecutionResult download(const DownloadModelPayload &, const CancelToken *, DownloadProgressHandler) override {
        ++calls;
        return result;
    }
    int calls = 0;
};

class FakeSession final : public ITranslationSession {
public:
    void initialize_backend() override {
    }
    std::string active_backend_label() const override {
        return "fake";
    }
    bool is_loaded() const override {
        return loaded;
    }
    ExecutionResult load(const LoadModelPayload &) override {
        loaded = true;
        if (load_hook) load_hook();
        if (load_results.empty()) return load_result;
        const ExecutionResult result = load_results.front();
        load_results.erase(load_results.begin());
        return result;
    }
    ExecutionResult unload() override {
        ++unload_calls;
        loaded = false;
        if (unload_hook) unload_hook();
        return unload_result;
    }
    ExecutionResult translate(const TranslatePipelinePayload &, TranslationResetHandler, TranslationTokenHandler,
                              const CancelToken *) override {
        ++translate_calls;
        if (preempt_hook) preempt_hook();
        if (throw_translate) throw std::runtime_error(translate_error);
        return translate_result;
    }
    bool loaded = true;
    int translate_calls = 0;
    int unload_calls = 0;
    ExecutionResult load_result{ExecutionOutcome::Completed, {}};
    std::vector<ExecutionResult> load_results;
    ExecutionResult unload_result{ExecutionOutcome::Completed, {}};
    ExecutionResult translate_result{ExecutionOutcome::Completed, {}};
    std::function<void()> preempt_hook;
    std::function<void()> load_hook;
    std::function<void()> unload_hook;
    bool throw_translate = false;
    std::string translate_error = "translation exception";
};

struct Harness {
    FakeDownloader *downloader;
    FakeSession *session;
    TaskOrchestrator orchestrator;
    std::vector<TaskState> states;
    std::vector<std::string> statuses;
    std::vector<std::string> failures;
    int load_finished_calls = 0;
    bool last_load_success = false;
    int unload_finished_calls = 0;

    Harness()
        : downloader(new FakeDownloader()), session(new FakeSession()), orchestrator(TaskExecutors{std::unique_ptr<IModelDownloader>(downloader), std::unique_ptr<ITranslationSession>(session)}) {
        TaskOrchestratorCallbacks callbacks;
        callbacks.on_task_state_changed = [this](std::uint64_t, TaskState state) { states.push_back(state); };
        callbacks.on_status = [this](const std::string &message, bool) { statuses.push_back(message); };
        callbacks.on_task_failed = [this](std::uint64_t, const std::string &message) { failures.push_back(message); };
        callbacks.on_model_load_finished = [this](bool success, const std::string &) {
            ++load_finished_calls;
            last_load_success = success;
        };
        callbacks.on_model_unload_finished = [this] { ++unload_finished_calls; };
        orchestrator.set_callbacks(std::move(callbacks));
    }
};

TranslatePipelinePayload translate_payload() {
    return {"hello", "Chinese", "English", false, false};
}
}  // namespace

TEST(TaskOrchestrator, DownloadCancellationAlwaysFinalizes) {
    Harness h;
    h.downloader->result = {ExecutionOutcome::Cancelled, "cancelled"};
    const TaskId id = h.orchestrator.submit_download_model();
    h.orchestrator.process_next();
    EXPECT_EQ(h.orchestrator.task_state(id), TaskState::Cancelled);
    EXPECT_EQ(std::count(h.states.begin(), h.states.end(), TaskState::Completed), 0);
    EXPECT_EQ(std::count(h.states.begin(), h.states.end(), TaskState::Running), 1);
    EXPECT_EQ(std::count_if(h.states.begin(), h.states.end(), [](TaskState state) {
                  return state == TaskState::Completed || state == TaskState::Failed ||
                         state == TaskState::Cancelled || state == TaskState::Preempted;
              }),
              1);
}

TEST(TaskOrchestrator, UnloadFailureDoesNotRemainRunning) {
    Harness h;
    h.session->unload_result = {ExecutionOutcome::Failed, "unload failed"};
    const TaskId id = h.orchestrator.submit_unload_model();
    h.orchestrator.process_next();
    EXPECT_EQ(h.orchestrator.task_state(id), TaskState::Failed);
}

TEST(TaskOrchestrator, FailedLoadClearsPreviouslyLoadedModel) {
    Harness h;
    h.session->load_results = {
        {ExecutionOutcome::Completed, {}},
        {ExecutionOutcome::Failed, "replacement failed"},
    };
    const TaskId first = h.orchestrator.submit_load_model(TaskPriority::Normal);
    const TaskId second = h.orchestrator.submit_load_model(TaskPriority::Normal);

    h.orchestrator.process_next();

    EXPECT_EQ(h.orchestrator.task_state(first), TaskState::Completed);
    EXPECT_EQ(h.orchestrator.task_state(second), TaskState::Failed);
    EXPECT_FALSE(h.session->is_loaded());
    EXPECT_FALSE(h.orchestrator.is_model_loaded());
    EXPECT_EQ(h.session->unload_calls, 1);
}

TEST(TaskOrchestrator, RunningLoadCannotBeCancelled) {
    Harness h;
    std::mutex mutex;
    std::condition_variable condition;
    bool started = false;
    bool release = false;
    h.session->load_hook = [&] {
        std::unique_lock<std::mutex> lock(mutex);
        started = true;
        condition.notify_all();
        condition.wait(lock, [&] { return release; });
    };
    const TaskId id = h.orchestrator.submit_load_model();
    std::thread worker([&] { h.orchestrator.process_next(); });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return started; });
    }

    EXPECT_FALSE(h.orchestrator.cancel(id));
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    condition.notify_all();
    worker.join();

    EXPECT_EQ(h.orchestrator.task_state(id), TaskState::Completed);
    EXPECT_TRUE(h.session->is_loaded());
    EXPECT_EQ(h.load_finished_calls, 1);
    EXPECT_TRUE(h.last_load_success);
}

TEST(TaskOrchestrator, RunningUnloadCannotBeCancelled) {
    Harness h;
    std::mutex mutex;
    std::condition_variable condition;
    bool started = false;
    bool release = false;
    h.session->unload_hook = [&] {
        std::unique_lock<std::mutex> lock(mutex);
        started = true;
        condition.notify_all();
        condition.wait(lock, [&] { return release; });
    };
    const TaskId id = h.orchestrator.submit_unload_model();
    std::thread worker([&] { h.orchestrator.process_next(); });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return started; });
    }

    EXPECT_FALSE(h.orchestrator.cancel(id));
    {
        std::lock_guard<std::mutex> lock(mutex);
        release = true;
    }
    condition.notify_all();
    worker.join();

    EXPECT_EQ(h.orchestrator.task_state(id), TaskState::Completed);
    EXPECT_FALSE(h.session->is_loaded());
    EXPECT_EQ(h.unload_finished_calls, 1);
}

TEST(TaskOrchestrator, BackgroundTranslationPreemptIsPreempted) {
    Harness h;
    h.session->translate_result = {ExecutionOutcome::Cancelled, {}};
    const TaskId id = h.orchestrator.submit_translate_pipeline(translate_payload(), TaskPriority::Background);
    h.session->preempt_hook = [&h] { EXPECT_TRUE(h.orchestrator.preempt_running_background()); };
    h.orchestrator.process_next();
    EXPECT_EQ(h.orchestrator.task_state(id), TaskState::Preempted);
}

TEST(TaskOrchestrator, PauseThenUserCancellationRemainsPreempted) {
    Harness h;
    h.session->translate_result = {ExecutionOutcome::Cancelled, {}};
    const TaskId id = h.orchestrator.submit_translate_pipeline(translate_payload(), TaskPriority::Background);
    h.session->preempt_hook = [&h, id] {
        EXPECT_TRUE(h.orchestrator.preempt_running_background());
        EXPECT_TRUE(h.orchestrator.cancel(id));
        h.session->preempt_hook = nullptr;
    };

    h.orchestrator.process_next();

    EXPECT_EQ(h.orchestrator.task_state(id), TaskState::Preempted);
}

TEST(TaskOrchestrator, InteractiveThenUserCancellationRemainsPreempted) {
    Harness h;
    h.session->translate_result = {ExecutionOutcome::Cancelled, {}};
    const TaskId id = h.orchestrator.submit_translate_pipeline(translate_payload(), TaskPriority::Background);
    h.session->preempt_hook = [&h, id] {
        h.orchestrator.submit_translate_pipeline(translate_payload(), TaskPriority::Interactive);
        EXPECT_TRUE(h.orchestrator.cancel(id));
        h.session->preempt_hook = nullptr;
    };

    h.orchestrator.process_next();

    EXPECT_EQ(h.orchestrator.task_state(id), TaskState::Preempted);
}

TEST(TaskOrchestrator, UserCancellationIsCancelled) {
    Harness h;
    h.session->translate_result = {ExecutionOutcome::Cancelled, {}};
    const TaskId id = h.orchestrator.submit_translate_pipeline(translate_payload());
    EXPECT_TRUE(h.orchestrator.cancel(id));
    h.orchestrator.process_next();
    EXPECT_EQ(h.orchestrator.task_state(id), TaskState::Cancelled);
}

TEST(TaskOrchestrator, TranslationFailureReportsDetailedStatus) {
    Harness h;
    h.session->translate_result = {ExecutionOutcome::Failed, "context limit"};
    const TaskId id = h.orchestrator.submit_translate_pipeline(translate_payload());

    h.orchestrator.process_next();

    EXPECT_EQ(h.orchestrator.task_state(id), TaskState::Failed);
    EXPECT_NE(std::find(h.statuses.begin(), h.statuses.end(), "Error: context limit"), h.statuses.end());
    ASSERT_EQ(h.failures.size(), 1u);
    EXPECT_EQ(h.failures.front(), "context limit");
}

TEST(TaskOrchestrator, TranslationExceptionReportsDetailedStatus) {
    Harness h;
    h.session->throw_translate = true;
    const TaskId id = h.orchestrator.submit_translate_pipeline(translate_payload());

    h.orchestrator.process_next();

    EXPECT_EQ(h.orchestrator.task_state(id), TaskState::Failed);
    EXPECT_NE(std::find(h.statuses.begin(), h.statuses.end(), "Error: translation exception"), h.statuses.end());
    ASSERT_EQ(h.failures.size(), 1u);
    EXPECT_EQ(h.failures.front(), "translation exception");
}

TEST(TaskOrchestrator, FailedTranslationPreemptedHasNoFailureReporting) {
    Harness h;
    h.session->translate_result = {ExecutionOutcome::Failed, "late failure"};
    const TaskId id = h.orchestrator.submit_translate_pipeline(translate_payload(), TaskPriority::Background);
    h.session->preempt_hook = [&h] {
        EXPECT_TRUE(h.orchestrator.preempt_running_background());
        h.session->preempt_hook = nullptr;
    };

    h.orchestrator.process_next();

    EXPECT_EQ(h.orchestrator.task_state(id), TaskState::Preempted);
    EXPECT_EQ(std::find(h.statuses.begin(), h.statuses.end(), "Error: late failure"), h.statuses.end());
    EXPECT_TRUE(h.failures.empty());
}

TEST(TaskOrchestrator, QueueRetainsPriorityOrder) {
    Harness h;
    h.session->translate_result = {ExecutionOutcome::Completed, {}};
    const TaskId normal = h.orchestrator.submit_translate_pipeline(translate_payload(), TaskPriority::Normal);
    const TaskId background = h.orchestrator.submit_translate_pipeline(translate_payload(), TaskPriority::Background);
    h.orchestrator.process_next();
    EXPECT_EQ(h.orchestrator.task_state(background), TaskState::Completed);
    EXPECT_EQ(h.orchestrator.task_state(normal), TaskState::Completed);
}
