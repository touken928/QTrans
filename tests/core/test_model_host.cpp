#include "qtrans/core.h"
#include "invocation_scheduler.h"
#include "model_host_test_access.h"
#include "prompt_profiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

namespace qtrans::core {
namespace {

InvocationRequest request() {
    InvocationRequest value;
    value.model = ModelId{"demo"};
    value.input = TranslationInput{"hello", std::nullopt, LanguageTag{"fr"}};
    value.work_class = WorkClass::NativeInteractive;
    return value;
}

struct EventLog {
    void add(const InvocationEvent &event) {
        std::lock_guard lock(mutex);
        events.push_back(event);
        if (std::holds_alternative<InvocationFinished>(event)) finished.notify_all();
    }

    bool wait_for_terminal() {
        std::unique_lock lock(mutex);
        return finished.wait_for(lock, std::chrono::seconds(2), [&] {
            return std::any_of(events.begin(), events.end(), [](const InvocationEvent &event) {
                return std::holds_alternative<InvocationFinished>(event);
            });
        });
    }

    std::vector<InvocationEvent> events;
    std::mutex mutex;
    std::condition_variable finished;
};

struct Gate {
    void wait() {
        std::unique_lock lock(mutex);
        entered = true;
        ready.notify_all();
        released.wait(lock, [&] { return open; });
    }

    void await_entered() {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(ready.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }));
    }

    void release() {
        {
            std::lock_guard lock(mutex);
            open = true;
        }
        released.notify_all();
    }

    std::mutex mutex;
    std::condition_variable ready;
    std::condition_variable released;
    bool entered = false;
    bool open = false;
};

TEST(ModelHost, LifecycleAndAdmissionStatesAreExplicit) {
    test::ModelHostHooks hooks;
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    EXPECT_EQ(host.snapshot().state, LifecycleState::Unloaded);
    EXPECT_EQ(host.submit(request()).failure.code, FailureCode::NotLoaded);
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    EXPECT_EQ(host.snapshot().state, LifecycleState::Ready);
    EXPECT_FALSE(host.load({ModelId{"other"}, {}}));
    ASSERT_TRUE(host.unload());
    EXPECT_EQ(host.snapshot().state, LifecycleState::Unloaded);
}

TEST(ModelHostPrompt, ProfilesPreserveConversationHistoryAndCapabilityErrors) {
    host_detail::PromptProfile profile;
    ASSERT_FALSE(host_detail::select_prompt_profile(ModelId{"hymt2-q4"}, profile));
    ConversationInput conversation{{Message{Role::System, "system"}, Message{Role::User, "first"},
                                    Message{Role::Assistant, "answer"}, Message{Role::User, "second"}}};
    std::string prompt;
    ASSERT_FALSE(profile.render(conversation, prompt));
    EXPECT_NE(prompt.find("system"), std::string::npos);
    EXPECT_NE(prompt.find("first"), std::string::npos);
    EXPECT_NE(prompt.find("answer"), std::string::npos);
    EXPECT_NE(prompt.find("second"), std::string::npos);
    EXPECT_NE(prompt.rfind("Assistant"), std::string::npos);
    EXPECT_EQ(prompt, std::string(u8"<\xEF\xBD\x9Chy_begin\xE2\x96\x81of\xE2\x96\x81sentence\xEF\xBD\x9C>") +
                          u8"<\xEF\xBD\x9Chy_User\xEF\xBD\x9C>System instructions:\nsystem" +
                          u8"<\xEF\xBD\x9Chy_User\xEF\xBD\x9C>first" +
                          u8"<\xEF\xBD\x9Chy_Assistant\xEF\xBD\x9C>answer" +
                          u8"<\xEF\xBD\x9Chy_User\xEF\xBD\x9C>second" +
                          u8"<\xEF\xBD\x9Chy_Assistant\xEF\xBD\x9C>");

    ASSERT_FALSE(host_detail::select_prompt_profile(ModelId{"hymt2-7b-q4"}, profile));
    EXPECT_EQ(profile.render(conversation, prompt).code, FailureCode::UnsupportedCapability);
}

TEST(ModelHostPrompt, HymtTemplatesMatchEstablishedControlBytes) {
    const TranslationInput translation{"hello", std::nullopt, LanguageTag{"English"}};
    std::string prompt;
    host_detail::PromptProfile profile;
    ASSERT_FALSE(host_detail::select_prompt_profile(ModelId{"hymt2-q4"}, profile));
    ASSERT_FALSE(profile.render(translation, prompt));
    EXPECT_EQ(prompt, std::string(u8"<\xEF\xBD\x9Chy_begin\xE2\x96\x81of\xE2\x96\x81sentence\xEF\xBD\x9C>") +
                          u8"<\xEF\xBD\x9Chy_User\xEF\xBD\x9C>Translate the following segment into English, without additional explanation.\n\nhello" +
                          u8"<\xEF\xBD\x9Chy_Assistant\xEF\xBD\x9C>");
    ASSERT_FALSE(host_detail::select_prompt_profile(ModelId{"hymt2-7b-q4"}, profile));
    ASSERT_FALSE(profile.render(translation, prompt));
    EXPECT_EQ(prompt, "<|startoftext|>Translate the following segment into English, without additional explanation.\n\nhello<|extra_0|>");
}

TEST(ModelHostScheduler, PerClassFifoAndInteractiveFairnessAreBounded) {
    host_detail::InvocationScheduler scheduler;
    for (std::uint64_t id = 1; id <= host_detail::InvocationScheduler::kCapacity; ++id)
        ASSERT_TRUE(scheduler.push({InvocationId{id}, WorkClass::Batch}));
    EXPECT_FALSE(scheduler.push({InvocationId{100}, WorkClass::Batch}));

    host_detail::InvocationScheduler fair;
    ASSERT_TRUE(fair.push({InvocationId{1}, WorkClass::NativeInteractive}));
    ASSERT_TRUE(fair.push({InvocationId{2}, WorkClass::ApiInteractive}));
    ASSERT_TRUE(fair.push({InvocationId{3}, WorkClass::NativeNormal}));
    ASSERT_TRUE(fair.push({InvocationId{4}, WorkClass::NativeInteractive}));
    ASSERT_TRUE(fair.push({InvocationId{5}, WorkClass::Batch}));
    EXPECT_EQ(fair.pop()->id.value, 1U);
    EXPECT_EQ(fair.pop()->id.value, 2U);
    EXPECT_EQ(fair.pop()->id.value, 3U);
    EXPECT_EQ(fair.pop()->id.value, 5U);
    EXPECT_EQ(fair.pop()->id.value, 4U);
}

TEST(ModelHostRuntime, FakeExecutionReportsUsageTimingAndDeltas) {
    test::ModelHostHooks hooks;
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    EventLog log;
    ASSERT_TRUE(host.submit(request(), [&](const InvocationEvent &event) { log.add(event); }));
    ASSERT_TRUE(log.wait_for_terminal());
    std::lock_guard lock(log.mutex);
    EXPECT_TRUE(std::any_of(log.events.begin(), log.events.end(), [](const InvocationEvent &event) {
        return std::holds_alternative<InvocationDelta>(event);
    }));
    const auto &result = std::get<InvocationFinished>(log.events.back()).result;
    EXPECT_GT(result.usage.input_tokens, 0U);
    EXPECT_GT(result.usage.output_tokens, 0U);
    EXPECT_GE(result.timing.generation_milliseconds, 0U);
}

TEST(ModelHostRuntime, SamplingIsPropagatedAndIncompleteUtf8IsDiscarded) {
    SamplingOptions observed;
    test::ModelHostHooks hooks;
    hooks.generate = [&](std::string_view, const SamplingOptions &sampling,
                         const std::function<void(std::string_view)> &on_delta,
                         const std::function<bool()> &) {
        observed = sampling;
        test::TestGeneration result;
        result.output = "A";
        result.output_tokens = 1;
        on_delta("\xE2");
        on_delta("\x82");
        on_delta("\xAC");
        on_delta("\xFF\x80");
        on_delta("\xF0\x9F");
        return result;
    };
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    auto value = request();
    value.sampling.max_output_tokens = 32;
    value.sampling.temperature = 0.2f;
    value.sampling.top_p = 0.8f;
    value.sampling.seed = 42;
    EventLog log;
    ASSERT_TRUE(host.submit(value, [&](const InvocationEvent &event) { log.add(event); }));
    ASSERT_TRUE(log.wait_for_terminal());
    EXPECT_EQ(observed.max_output_tokens, 32U);
    EXPECT_FLOAT_EQ(observed.temperature, 0.2f);
    EXPECT_FLOAT_EQ(observed.top_p, 0.8f);
    EXPECT_EQ(observed.seed, 42U);
    std::lock_guard lock(log.mutex);
    for (const auto &event : log.events) {
        if (const auto *delta = std::get_if<InvocationDelta>(&event)) EXPECT_EQ(delta->text, "€");
    }
}

TEST(ModelHostScheduler, InteractivePreemptsOnlyRunningBatch) {
    Gate gate;
    test::ModelHostHooks hooks;
    hooks.before_invocation = [&] { gate.wait(); };
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    auto batch = request();
    batch.work_class = WorkClass::Batch;
    EventLog batch_log;
    const SubmitResult batch_submission = host.submit(batch, [&](const InvocationEvent &event) { batch_log.add(event); });
    ASSERT_TRUE(batch_submission);
    gate.await_entered();
    ASSERT_TRUE(host.submit(request()));
    gate.release();
    ASSERT_TRUE(batch_log.wait_for_terminal());
    std::lock_guard lock(batch_log.mutex);
    const auto &result = std::get<InvocationFinished>(batch_log.events.back()).result;
    EXPECT_EQ(result.stop_reason, StopReason::Preempted);
}

TEST(ModelHostScheduler, QueuedBatchRemainsQueuedAfterInteractivePreemption) {
    Gate gate;
    test::ModelHostHooks hooks;
    hooks.before_invocation = [&] { gate.wait(); };
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    auto batch = request();
    batch.work_class = WorkClass::Batch;
    std::mutex mutex;
    std::condition_variable finished;
    std::vector<InvocationResult> results;
    const auto observer = [&](const InvocationEvent &event) {
        if (const auto *terminal = std::get_if<InvocationFinished>(&event)) {
            std::lock_guard lock(mutex);
            results.push_back(terminal->result);
            finished.notify_all();
        }
    };
    ASSERT_TRUE(host.submit(batch, observer));
    gate.await_entered();
    ASSERT_TRUE(host.submit(batch, observer));
    ASSERT_TRUE(host.submit(request(), observer));
    gate.release();
    std::unique_lock lock(mutex);
    ASSERT_TRUE(finished.wait_for(lock, std::chrono::seconds(2), [&] { return results.size() == 3; }));
    EXPECT_EQ(std::count_if(results.begin(), results.end(), [](const InvocationResult &result) {
                  return result.stop_reason == StopReason::Preempted;
              }),
              1);
    EXPECT_EQ(std::count_if(results.begin(), results.end(), [](const InvocationResult &result) {
                  return result.stop_reason == StopReason::None;
              }),
              2);
}

TEST(ModelHostRuntime, DeadlineCanCancelDuringGeneration) {
    test::ModelHostHooks hooks;
    hooks.generate = [](std::string_view, const SamplingOptions &, const std::function<void(std::string_view)> &,
                        const std::function<bool()> &should_stop) {
        test::TestGeneration result;
        while (!should_stop()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        result.failure = {FailureCode::Cancelled, "deadline reached"};
        return result;
    };
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    auto value = request();
    value.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
    EventLog log;
    ASSERT_TRUE(host.submit(value, [&](const InvocationEvent &event) { log.add(event); }));
    ASSERT_TRUE(log.wait_for_terminal());
    std::lock_guard lock(log.mutex);
    const auto &result = std::get<InvocationFinished>(log.events.back()).result;
    EXPECT_EQ(result.stop_reason, StopReason::Deadline);
    ASSERT_TRUE(result.failure.has_value());
    EXPECT_EQ(result.failure->code, FailureCode::Deadline);
    EXPECT_GE(result.timing.generation_milliseconds, 0U);
}

TEST(ModelHostRuntime, QueueTimingExcludesGenerationDuration) {
    test::ModelHostHooks hooks;
    hooks.generate = [](std::string_view, const SamplingOptions &, const std::function<void(std::string_view)> &,
                        const std::function<bool()> &) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
        test::TestGeneration result;
        result.output = "done";
        result.output_tokens = 2;
        return result;
    };
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    EventLog log;
    ASSERT_TRUE(host.submit(request(), [&](const InvocationEvent &event) { log.add(event); }));
    ASSERT_TRUE(log.wait_for_terminal());
    std::lock_guard lock(log.mutex);
    const auto &result = std::get<InvocationFinished>(log.events.back()).result;
    EXPECT_GE(result.timing.generation_milliseconds, 20U);
    EXPECT_LT(result.timing.queue_milliseconds, result.timing.generation_milliseconds);
}

TEST(ModelHostRuntime, PartialUsageSurvivesUserCancellation) {
    std::atomic<bool> entered{false};
    test::ModelHostHooks hooks;
    hooks.generate = [&](std::string_view, const SamplingOptions &, const std::function<void(std::string_view)> &on_delta,
                         const std::function<bool()> &should_stop) {
        entered = true;
        while (!should_stop()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        on_delta("partial");
        test::TestGeneration result;
        result.output = "partial";
        result.prompt_tokens = 7;
        result.output_tokens = 3;
        result.failure = {FailureCode::Cancelled, "cancelled after partial output"};
        return result;
    };
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    EventLog log;
    const SubmitResult submitted = host.submit(request(), [&](const InvocationEvent &event) { log.add(event); });
    ASSERT_TRUE(submitted);
    while (!entered) std::this_thread::yield();
    ASSERT_TRUE(host.cancel(submitted.id));
    ASSERT_TRUE(log.wait_for_terminal());
    std::lock_guard lock(log.mutex);
    const auto &result = std::get<InvocationFinished>(log.events.back()).result;
    EXPECT_EQ(result.stop_reason, StopReason::UserCancel);
    EXPECT_EQ(result.usage.input_tokens, 7U);
    EXPECT_EQ(result.usage.output_tokens, 3U);
    EXPECT_EQ(result.output, "partial");
}

TEST(ModelHostRuntime, SplitTranslationAggregatesUsageAndLengthAcrossChunks) {
    std::atomic<int> calls{0};
    test::ModelHostHooks hooks;
    hooks.generate = [&](std::string_view, const SamplingOptions &, const std::function<void(std::string_view)> &on_delta,
                         const std::function<bool()> &) {
        const int call = calls.fetch_add(1);
        test::TestGeneration result;
        result.output = "x";
        result.prompt_tokens = 5;
        result.output_tokens = 4;
        result.reached_length = call == 0;
        on_delta("x");
        return result;
    };
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    auto value = request();
    value.input = TranslationInput{std::string(9000, 'a'), std::nullopt, LanguageTag{"English"}, OverflowPolicy::Split};
    EventLog log;
    ASSERT_TRUE(host.submit(value, [&](const InvocationEvent &event) { log.add(event); }));
    ASSERT_TRUE(log.wait_for_terminal());
    std::lock_guard lock(log.mutex);
    const auto &result = std::get<InvocationFinished>(log.events.back()).result;
    EXPECT_GT(calls.load(), 1);
    EXPECT_EQ(result.finish_reason, FinishReason::Length);
    EXPECT_EQ(result.usage.input_tokens, static_cast<std::uint64_t>(calls.load() * 5));
    EXPECT_EQ(result.usage.output_tokens, static_cast<std::uint64_t>(calls.load() * 4));
}

TEST(ModelHost, RequestIsDataOnlyAndEventsAreSequenced) {
    test::ModelHostHooks hooks;
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    EventLog log;
    const SubmitResult submitted = host.submit(request(), [&](const InvocationEvent &event) { log.add(event); });
    ASSERT_TRUE(submitted);
    ASSERT_TRUE(log.wait_for_terminal());

    std::lock_guard lock(log.mutex);
    std::uint64_t expected = 1;
    for (const auto &event : log.events) {
        const auto sequence = std::visit([](const auto &value) { return value.sequence; }, event);
        EXPECT_EQ(sequence, expected++);
    }
}

TEST(ModelHost, ObserversRunOnDispatchThreadAndCanReenter) {
    const auto caller = std::this_thread::get_id();
    std::atomic<bool> different{false};
    std::atomic<bool> reentered{false};
    test::ModelHostHooks hooks;
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    EventLog log;
    ASSERT_TRUE(host.submit(request(), [&](const InvocationEvent &event) {
        different = std::this_thread::get_id() != caller;
        if (std::holds_alternative<InvocationStarted>(event)) {
            host.unload();
            host.cancel(std::get<InvocationStarted>(event).id);
            host.shutdown();
            reentered = true;
        }
        log.add(event);
    }));
    ASSERT_TRUE(log.wait_for_terminal());
    EXPECT_TRUE(different);
    EXPECT_TRUE(reentered);
}

TEST(ModelHost, ObserverExceptionsAreIsolatedAndBothSinksReceiveEvents) {
    EventLog request_log;
    EventLog global_log;
    ModelHost::Options options;
    options.event_sink = [&](const InvocationEvent &event) {
        global_log.add(event);
        throw std::runtime_error("global observer");
    };
    test::ModelHostHooks hooks;
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host(std::move(options));
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    ASSERT_TRUE(host.submit(request(), [&](const InvocationEvent &event) {
        request_log.add(event);
        throw std::runtime_error("request observer");
    }));
    ASSERT_TRUE(request_log.wait_for_terminal());
    ASSERT_TRUE(global_log.wait_for_terminal());
    EXPECT_EQ(request_log.events.size(), global_log.events.size());
    EXPECT_EQ(host.snapshot().active_invocations, 0U);
}

TEST(ModelHost, ShutdownCancelsQueuedWorkAndSettlesEachInvocationOnce) {
    Gate gate;
    test::ModelHostHooks hooks;
    hooks.before_invocation = [&] { gate.wait(); };
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    std::mutex mutex;
    std::condition_variable finished;
    std::vector<InvocationResult> results;
    const auto observer = [&](const InvocationEvent &event) {
        if (const auto *terminal = std::get_if<InvocationFinished>(&event)) {
            std::lock_guard lock(mutex);
            results.push_back(terminal->result);
            finished.notify_all();
        }
    };
    std::vector<SubmitResult> submitted;
    for (int i = 0; i < 8; ++i) submitted.push_back(host.submit(request(), observer));
    gate.await_entered();
    std::thread stopper([&] { EXPECT_TRUE(host.shutdown()); });
    while (host.snapshot().state != LifecycleState::Draining) std::this_thread::yield();
    gate.release();
    stopper.join();
    std::unique_lock lock(mutex);
    ASSERT_TRUE(finished.wait_for(lock, std::chrono::seconds(2), [&] { return results.size() == submitted.size(); }));
    EXPECT_EQ(host.snapshot().state, LifecycleState::Stopped);
    EXPECT_TRUE(std::all_of(results.begin(), results.end(), [](const InvocationResult &result) {
        return result.finish_reason == FinishReason::Cancelled && result.stop_reason == StopReason::Shutdown;
    }));
}

TEST(ModelHost, StopReasonPrecedenceIsShutdownUserDeadlinePreemption) {
    Gate gate;
    test::ModelHostHooks hooks;
    hooks.before_invocation = [&] { gate.wait(); };
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    EventLog log;
    auto value = request();
    value.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    const SubmitResult submitted = host.submit(value, [&](const InvocationEvent &event) { log.add(event); });
    ASSERT_TRUE(submitted);
    gate.await_entered();
    std::thread stopper([&] { EXPECT_TRUE(host.shutdown()); });
    while (host.snapshot().state != LifecycleState::Draining) std::this_thread::yield();
    gate.release();
    stopper.join();
    ASSERT_TRUE(log.wait_for_terminal());
    std::lock_guard lock(log.mutex);
    const auto &result = std::get<InvocationFinished>(log.events.back()).result;
    EXPECT_EQ(result.stop_reason, StopReason::Shutdown);
}

TEST(ModelHost, DispatchShutdownFirstDefersToOneExternalFinalizer) {
    Gate gate;
    test::ModelHostHooks hooks;
    hooks.before_invocation = [&] { gate.wait(); };
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    std::mutex mutex;
    std::condition_variable started_cv;
    bool started = false;
    EventLog log;
    ASSERT_TRUE(host.submit(request(), [&](const InvocationEvent &event) {
        if (std::holds_alternative<InvocationStarted>(event)) {
            const OperationResult result = host.shutdown();
            EXPECT_TRUE(result);
            EXPECT_TRUE(result.deferred);
            {
                std::lock_guard lock(mutex);
                started = true;
            }
            started_cv.notify_one();
        }
        log.add(event);
    }));
    gate.await_entered();
    gate.release();
    {
        std::unique_lock lock(mutex);
        ASSERT_TRUE(started_cv.wait_for(lock, std::chrono::seconds(2), [&] { return started; }));
    }
    ASSERT_TRUE(host.shutdown());
    ASSERT_TRUE(log.wait_for_terminal());
    EXPECT_EQ(host.snapshot().state, LifecycleState::Stopped);
}

TEST(ModelHost, ObserverOnlyShutdownStartsFinalizerAndReachesStopped) {
    test::ModelHostHooks hooks;
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    EventLog log;
    std::atomic<bool> deferred{false};
    ASSERT_TRUE(host.submit(request(), [&](const InvocationEvent &event) {
        if (std::holds_alternative<InvocationStarted>(event)) deferred = host.shutdown().deferred;
        log.add(event);
    }));
    ASSERT_TRUE(log.wait_for_terminal());
    EXPECT_TRUE(deferred);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (host.snapshot().state != LifecycleState::Stopped && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    EXPECT_EQ(host.snapshot().state, LifecycleState::Stopped);
}

TEST(ModelHost, ShutdownUnloadsReadyModelOnRuntimeOwnerThread) {
    std::thread::id unload_thread;
    test::ModelHostHooks hooks;
    hooks.unload_runtime = [&] {
        unload_thread = std::this_thread::get_id();
        return Failure{FailureCode::Runtime, "unload failed"};
    };
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    const auto caller = std::this_thread::get_id();
    ASSERT_TRUE(host.shutdown());
    EXPECT_NE(unload_thread, caller);
    EXPECT_EQ(host.snapshot().state, LifecycleState::Stopped);
    ASSERT_TRUE(host.snapshot().failure.has_value());
    EXPECT_EQ(host.snapshot().failure->code, FailureCode::Runtime);
}

TEST(ModelHost, ExternalFirstShutdownAllowsReentrantObserverRequest) {
    Gate gate;
    test::ModelHostHooks hooks;
    hooks.before_invocation = [&] { gate.wait(); };
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    EventLog log;
    ASSERT_TRUE(host.submit(request(), [&](const InvocationEvent &event) {
        if (std::holds_alternative<InvocationFinished>(event)) {
            const OperationResult result = host.shutdown();
            EXPECT_TRUE(result);
            EXPECT_TRUE(result.deferred);
        }
        log.add(event);
    }));
    gate.await_entered();
    std::thread external([&] { EXPECT_TRUE(host.shutdown()); });
    while (host.snapshot().state != LifecycleState::Draining) std::this_thread::yield();
    gate.release();
    external.join();
    ASSERT_TRUE(log.wait_for_terminal());
    EXPECT_EQ(host.snapshot().state, LifecycleState::Stopped);
}

TEST(ModelHost, ConcurrentCancelAndShutdownDoNotDeadlock) {
    test::ModelHostHooks hooks;
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    EventLog log;
    const SubmitResult submitted = host.submit(request(), [&](const InvocationEvent &event) { log.add(event); });
    ASSERT_TRUE(submitted);
    std::thread cancel_thread([&] { submitted.handle.cancel(); });
    ASSERT_TRUE(host.shutdown());
    cancel_thread.join();
    ASSERT_TRUE(log.wait_for_terminal());
    std::lock_guard lock(log.mutex);
    EXPECT_EQ(std::count_if(log.events.begin(), log.events.end(), [](const InvocationEvent &event) {
                  return std::holds_alternative<InvocationFinished>(event);
              }),
              1);
}

TEST(ModelHost, LifecycleCommandFailuresRollBackState) {
    test::ModelHostHooks load_hooks;
    load_hooks.load_runtime = [](const ModelSpec &) { return Failure{FailureCode::Runtime, "load failed"}; };
    test::ScopedModelHostHooks load_scope(load_hooks);
    ModelHost load_host;
    EXPECT_FALSE(load_host.load({ModelId{"demo"}, {}}));
    EXPECT_EQ(load_host.snapshot().state, LifecycleState::Unloaded);

    test::ModelHostHooks unload_hooks;
    unload_hooks.unload_runtime = [] { return Failure{FailureCode::Runtime, "unload failed"}; };
    test::ScopedModelHostHooks unload_scope(unload_hooks);
    ModelHost unload_host;
    ASSERT_TRUE(unload_host.load({ModelId{"demo"}, {}}));
    EXPECT_FALSE(unload_host.unload());
    EXPECT_EQ(unload_host.snapshot().state, LifecycleState::Ready);
}

TEST(ModelHost, ShutdownWinsBlockedLoadWithoutPublishingReady) {
    Gate gate;
    test::ModelHostHooks hooks;
    hooks.load_runtime = [&](const ModelSpec &) {
        gate.wait();
        return Failure{};
    };
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    std::atomic<bool> load_finished{false};
    std::thread loader([&] {
        EXPECT_FALSE(host.load({ModelId{"demo"}, {}}));
        load_finished = true;
    });
    gate.await_entered();
    EXPECT_EQ(host.snapshot().state, LifecycleState::Loading);
    std::thread stopper([&] { EXPECT_TRUE(host.shutdown()); });
    while (host.snapshot().state != LifecycleState::Draining) std::this_thread::yield();
    gate.release();
    loader.join();
    stopper.join();
    EXPECT_TRUE(load_finished);
    EXPECT_EQ(host.snapshot().state, LifecycleState::Stopped);
    EXPECT_FALSE(host.snapshot().model.has_value());
}

TEST(ModelHost, CancellationRacesSettlementAtOneLinearizationPoint) {
    Gate gate;
    test::ModelHostHooks hooks;
    hooks.before_invocation = [&] { gate.wait(); };
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    EventLog log;
    const SubmitResult submitted = host.submit(request(), [&](const InvocationEvent &event) { log.add(event); });
    ASSERT_TRUE(submitted);
    gate.await_entered();
    ASSERT_TRUE(submitted.handle.cancel());
    EXPECT_FALSE(host.cancel(submitted.id));
    gate.release();
    ASSERT_TRUE(log.wait_for_terminal());
    std::lock_guard lock(log.mutex);
    const auto &result = std::get<InvocationFinished>(log.events.back()).result;
    EXPECT_EQ(result.stop_reason, StopReason::UserCancel);
    EXPECT_FALSE(submitted.handle.cancel());
    EXPECT_FALSE(host.cancel(submitted.id));
}

TEST(ModelHost, ExternalAndReentrantShutdownShareCoordinator) {
    Gate gate;
    test::ModelHostHooks hooks;
    hooks.before_invocation = [&] { gate.wait(); };
    test::ScopedModelHostHooks scoped_hooks(hooks);
    ModelHost host;
    ASSERT_TRUE(host.load({ModelId{"demo"}, {}}));
    EventLog log;
    ASSERT_TRUE(host.submit(request(), [&](const InvocationEvent &event) {
        if (std::holds_alternative<InvocationFinished>(event)) EXPECT_TRUE(host.shutdown());
        log.add(event);
    }));
    gate.await_entered();
    std::thread external([&] { EXPECT_TRUE(host.shutdown()); });
    while (host.snapshot().state != LifecycleState::Draining) std::this_thread::yield();
    gate.release();
    external.join();
    ASSERT_TRUE(log.wait_for_terminal());
    EXPECT_EQ(host.snapshot().state, LifecycleState::Stopped);
}

}
}  // namespace qtrans::core
