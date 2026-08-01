#include "qtrans/core.h"
#include "model_host_test_access.h"
#include "invocation_scheduler.h"
#include "model_runtime.h"
#include "stop_mapping.h"
#include "../text/utf8.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace qtrans::core {

namespace test {

namespace {
thread_local const ModelHostHooks *active_hooks = nullptr;
}

ScopedModelHostHooks::ScopedModelHostHooks(const ModelHostHooks &hooks)
    : effective_(default_model_host_hooks()), previous_(active_hooks) {
    if (hooks.load_runtime) effective_.load_runtime = hooks.load_runtime;
    if (hooks.unload_runtime) effective_.unload_runtime = hooks.unload_runtime;
    if (hooks.before_invocation) effective_.before_invocation = hooks.before_invocation;
    if (hooks.generate) effective_.generate = hooks.generate;
    active_hooks = &effective_;
}

ScopedModelHostHooks::~ScopedModelHostHooks() {
    active_hooks = previous_;
}

const ModelHostHooks *active_model_host_hooks() noexcept {
    return active_hooks;
}

ModelHostHooks default_model_host_hooks() {
    ModelHostHooks hooks;
    hooks.load_runtime = [](const ModelSpec &) { return Failure{}; };
    hooks.unload_runtime = [] { return Failure{}; };
    hooks.generate = [](std::string_view prompt, const SamplingOptions &, const std::function<void(std::string_view)> &on_delta,
                        const std::function<bool()> &should_stop) {
        TestGeneration generation;
        if (should_stop && should_stop()) {
            generation.failure = {FailureCode::Cancelled, "generation cancelled"};
            return generation;
        }
        generation.output = std::string(prompt);
        generation.output_tokens = static_cast<int>(generation.output.size());
        if (on_delta && !generation.output.empty()) on_delta(generation.output);
        return generation;
    };
    return hooks;
}

}  // namespace test

namespace {

thread_local void *dispatch_owner = nullptr;

int stop_priority(StopReason reason) {
    switch (reason) {
        case StopReason::Shutdown:
            return 4;
        case StopReason::UserCancel:
            return 3;
        case StopReason::Deadline:
            return 2;
        case StopReason::Preempted:
            return 1;
        case StopReason::None:
            return 0;
    }
    return 0;
}

FinishReason finish_reason(StopReason reason) {
    switch (reason) {
        case StopReason::Shutdown:
        case StopReason::UserCancel:
            return FinishReason::Cancelled;
        case StopReason::Deadline:
            return FinishReason::Deadline;
        case StopReason::Preempted:
            return FinishReason::Preempted;
        case StopReason::None:
            return FinishReason::Completed;
    }
    return FinishReason::Failed;
}

}  // namespace

struct ModelHost::Impl : std::enable_shared_from_this<ModelHost::Impl> {
    struct Invocation {
        InvocationRequest request;
        InvocationEventSink observer;
        InvocationId id;
        std::atomic<StopReason> stop{StopReason::None};
        std::uint64_t next_sequence = 1;
        std::chrono::steady_clock::time_point submitted_at = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point runtime_started;
        bool running = false;
    };

    struct DispatchItem {
        InvocationEvent event;
        InvocationEventSink observer;
    };

    explicit Impl(Options host_options, test::ModelHostHooks host_hooks)
        : options(std::move(host_options)), hooks(std::move(host_hooks)), runtime_thread(&Impl::run_runtime, this), dispatch_thread(&Impl::run_dispatch, this) {
    }

    ~Impl() {
        shutdown();
    }

    OperationResult command(std::function<Failure()> operation) {
        auto completion = std::make_shared<std::promise<Failure>>();
        {
            std::lock_guard lock(runtime_mutex);
            if (runtime_closed) return {false, {FailureCode::Shutdown, "model host is stopping"}};
            runtime_commands.emplace_back([operation = std::move(operation), completion] {
                try {
                    completion->set_value(operation());
                } catch (const std::exception &error) {
                    completion->set_value({FailureCode::Runtime, error.what()});
                } catch (...) {
                    completion->set_value({FailureCode::Runtime, "unknown runtime command failure"});
                }
            });
        }
        runtime_cv.notify_one();
        const Failure failure = completion->get_future().get();
        return {failure.code == FailureCode::None, failure};
    }

    void run_runtime() {
        try {
            host_detail::RuntimeInjection injection;
            if (hooks.generate) {
                const auto generator = hooks.generate;
                injection = [generator](const std::string &prompt, const runtime_detail::GenerationOptions &generation,
                                        const host_detail::RuntimeDeltaSink &on_delta,
                                        const host_detail::RuntimeStopPredicate &should_stop,
                                        host_detail::RuntimeExecution &result) {
                    const test::TestGeneration generated = generator(prompt, SamplingOptions{static_cast<std::uint32_t>(generation.max_output_tokens), generation.temperature, generation.top_p, generation.seed},
                                                                     on_delta, should_stop);
                    result.output = generated.output;
                    result.prompt_tokens = generated.prompt_tokens;
                    result.output_tokens = generated.output_tokens;
                    result.reached_length = generated.reached_length;
                    return generated.failure;
                };
            }
            runtime = std::make_unique<host_detail::ModelRuntime>(std::move(injection));
        } catch (const std::exception &error) {
            runtime_failure = {FailureCode::Runtime, error.what()};
        } catch (...) {
            runtime_failure = {FailureCode::Runtime, "failed to initialize model runtime"};
        }
        for (;;) {
            std::function<void()> operation;
            {
                std::unique_lock lock(runtime_mutex);
                runtime_cv.wait(lock, [this] { return runtime_closed || !runtime_commands.empty(); });
                if (runtime_commands.empty()) break;
                operation = std::move(runtime_commands.front());
                runtime_commands.pop_front();
            }
            try {
                operation();
            } catch (...) {
                // Every command wrapper owns a completion promise and catches its operation.
            }
        }
        runtime.reset();
    }

    void run_dispatch() {
        dispatch_owner = this;
        for (;;) {
            DispatchItem item;
            {
                std::unique_lock lock(dispatch_mutex);
                dispatch_cv.wait(lock, [this] { return dispatch_closed || !events.empty(); });
                if (events.empty() && dispatch_closed) break;
                item = std::move(events.front());
                events.pop_front();
            }
            if (options.event_sink) {
                try {
                    options.event_sink(item.event);
                } catch (...) {
                }
            }
            if (item.observer) {
                try {
                    item.observer(item.event);
                } catch (...) {
                }
            }
        }
        dispatch_owner = nullptr;
    }

    void publish(const Invocation &invocation, InvocationEvent event) {
        if (auto *delta = std::get_if<InvocationDelta>(&event)) {
            delta->text = sanitize_utf8(delta->text);
            if (delta->text.empty()) return;
        }
        {
            std::lock_guard lock(dispatch_mutex);
            events.push_back({std::move(event), invocation.observer});
        }
        dispatch_cv.notify_one();
    }

    bool request_stop(Invocation &invocation, StopReason reason) {
        auto current = invocation.stop.load(std::memory_order_acquire);
        while (stop_priority(reason) > stop_priority(current)) {
            if (invocation.stop.compare_exchange_weak(current, reason, std::memory_order_acq_rel)) return true;
        }
        return false;
    }

    OperationResult cancel_id(InvocationId id, StopReason reason) {
        std::lock_guard lock(state_mutex);
        const auto found = invocations.find(id.value);
        if (found == invocations.end())
            return {false, {FailureCode::InvalidRequest, "invocation is not active"}};
        return request_stop(*found->second, reason)
                   ? OperationResult{true, {}}
                   : OperationResult{false, {FailureCode::InvalidRequest, "invocation already stopped"}};
    }

    void settle(const std::shared_ptr<Invocation> &invocation,
                Failure failure = {},
                host_detail::RuntimeExecution execution = {},
                std::int64_t generation_milliseconds = 0) {
        InvocationResult result;
        {
            std::lock_guard lock(state_mutex);
            const auto found = invocations.find(invocation->id.value);
            if (found == invocations.end() || found->second.get() != invocation.get()) return;
            result.id = invocation->id;
            result.output = sanitize_utf8(execution.output);
            result.usage.input_tokens = execution.prompt_tokens;
            result.usage.output_tokens = execution.output_tokens;
            result.stop_reason = invocation->stop.load(std::memory_order_acquire);
            if (result.stop_reason != StopReason::None) {
                const auto outcome = host_detail::map_stop_reason(result.stop_reason);
                result.finish_reason = outcome.finish_reason;
                result.failure = outcome.failure;
            } else if (failure) {
                result.finish_reason = FinishReason::Failed;
                result.failure = std::move(failure);
            } else {
                result.finish_reason = execution.reached_length ? FinishReason::Length : FinishReason::Completed;
            }
            result.usage.total_tokens = result.usage.input_tokens + result.usage.output_tokens;
            result.timing.queue_milliseconds = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>((invocation->runtime_started.time_since_epoch().count() == 0
                                                                           ? std::chrono::steady_clock::now()
                                                                           : invocation->runtime_started) -
                                                                      invocation->submitted_at)
                    .count());
            result.timing.generation_milliseconds = static_cast<std::uint64_t>(generation_milliseconds);
            invocations.erase(found);
            invocation->running = false;
            if (state.active_invocations > 0) --state.active_invocations;
        }
        publish(*invocation, InvocationFinished{std::move(result), invocation->next_sequence++});
    }

    void execute(const std::shared_ptr<Invocation> &invocation) {
        {
            std::lock_guard lock(state_mutex);
            if (state.state == LifecycleState::Draining || state.state == LifecycleState::ShuttingDown)
                request_stop(*invocation, StopReason::Shutdown);
        }
        publish(*invocation, InvocationStarted{invocation->id, invocation->next_sequence++});
        if (invocation->stop.load(std::memory_order_acquire) != StopReason::None) {
            settle(invocation);
            return;
        }
        if (hooks.before_invocation) {
            try {
                hooks.before_invocation();
            } catch (...) {
                request_stop(*invocation, StopReason::Preempted);
            }
        }
        if (invocation->request.deadline && std::chrono::steady_clock::now() >= *invocation->request.deadline)
            request_stop(*invocation, StopReason::Deadline);
        {
            std::lock_guard lock(state_mutex);
            if (state.state == LifecycleState::Draining || state.state == LifecycleState::ShuttingDown)
                request_stop(*invocation, StopReason::Shutdown);
        }
        if (invocation->stop.load(std::memory_order_acquire) != StopReason::None) {
            settle(invocation);
            return;
        }
        const auto generation_started = std::chrono::steady_clock::now();
        host_detail::RuntimeExecution execution;
        Failure failure;
        if (runtime == nullptr) {
            failure = runtime_failure;
        } else {
            failure = runtime->execute(
                invocation->request.input, invocation->request.sampling,
                [&](std::string_view delta) { publish(*invocation, InvocationDelta{invocation->id, std::string(delta), invocation->next_sequence++}); },
                [&] { return should_stop(*invocation); }, execution);
        }
        const auto generation_finished = std::chrono::steady_clock::now();
        settle(invocation, std::move(failure), std::move(execution),
               std::chrono::duration_cast<std::chrono::milliseconds>(generation_finished - generation_started).count());
    }

    void run_scheduled() {
        std::shared_ptr<Invocation> invocation;
        {
            std::lock_guard lock(state_mutex);
            const auto scheduled = scheduler.pop();
            if (!scheduled) return;
            const auto found = invocations.find(scheduled->id.value);
            if (found == invocations.end()) return;
            invocation = found->second;
            invocation->running = true;
            invocation->runtime_started = std::chrono::steady_clock::now();
        }
        execute(invocation);
    }

    bool should_stop(const Invocation &invocation) const {
        if (invocation.stop.load(std::memory_order_acquire) != StopReason::None) return true;
        if (invocation.request.deadline && std::chrono::steady_clock::now() >= *invocation.request.deadline) {
            const_cast<Impl *>(this)->request_stop(const_cast<Invocation &>(invocation), StopReason::Deadline);
            return true;
        }
        return false;
    }

    void close_dispatch() {
        {
            std::lock_guard lock(dispatch_mutex);
            dispatch_closed = true;
        }
        dispatch_cv.notify_one();
        if (dispatch_thread.joinable() && dispatch_owner != this) dispatch_thread.join();
    }

    bool begin_shutdown() {
        std::lock_guard lock(state_mutex);
        if (state.state == LifecycleState::Stopped || state.state == LifecycleState::Draining) return false;
        state.state = LifecycleState::Draining;
        for (auto &[id, invocation] : invocations) request_stop(*invocation, StopReason::Shutdown);
        return true;
    }

    void ensure_finalizer() {
        std::lock_guard lock(finalizer_start_mutex);
        if (finalizer_started) return;
        finalizer_started = true;
        finalizer_thread = std::thread(&Impl::finalize_shutdown, this);
    }

    void finalize_shutdown() {
        auto unload_completion = std::make_shared<std::promise<Failure>>();
        bool unload_needed = false;
        {
            std::lock_guard lock(state_mutex);
            unload_needed = state.model.has_value();
        }
        {
            std::lock_guard lock(runtime_mutex);
            if (unload_needed) {
                runtime_commands.emplace_back([this, unload_completion] {
                    Failure failure;
                    try {
                        if (hooks.unload_runtime)
                            failure = hooks.unload_runtime();
                        else if (runtime != nullptr)
                            failure = runtime->unload();
                    } catch (const std::exception &error) {
                        failure = {FailureCode::Runtime, error.what()};
                    } catch (...) {
                        failure = {FailureCode::Runtime, "unknown model unload failure"};
                    }
                    unload_completion->set_value(failure);
                });
            } else {
                unload_completion->set_value({});
            }
            runtime_closed = true;
        }
        runtime_cv.notify_one();
        if (runtime_thread.joinable()) runtime_thread.join();
        const Failure unload_failure = unload_completion->get_future().get();
        close_dispatch();
        {
            std::lock_guard lock(state_mutex);
            state.failure = unload_failure ? std::optional<Failure>(unload_failure) : std::nullopt;
            state.state = LifecycleState::Stopped;
            state.model.reset();
        }
        {
            std::lock_guard lock(shutdown_completion_mutex);
            shutdown_complete = true;
        }
        shutdown_completion_cv.notify_all();
    }

    OperationResult shutdown() {
        const bool reentrant = dispatch_owner == this;
        if (reentrant) {
            begin_shutdown();
            ensure_finalizer();
            return {true, {}, true};
        }

        std::unique_lock coordinator(shutdown_mutex);
        begin_shutdown();
        ensure_finalizer();
        {
            std::unique_lock lock(shutdown_completion_mutex);
            shutdown_completion_cv.wait(lock, [this] { return shutdown_complete; });
        }
        if (finalizer_thread.joinable()) finalizer_thread.join();
        std::lock_guard lock(state_mutex);
        return {true, state.failure.value_or(Failure{}), false};
    }

    Options options;
    test::ModelHostHooks hooks;
    std::unique_ptr<host_detail::ModelRuntime> runtime;
    Failure runtime_failure;
    host_detail::InvocationScheduler scheduler;
    mutable std::mutex state_mutex;
    std::mutex shutdown_mutex;
    std::mutex finalizer_start_mutex;
    std::mutex shutdown_completion_mutex;
    std::condition_variable shutdown_completion_cv;
    bool finalizer_started = false;
    bool shutdown_complete = false;
    std::thread finalizer_thread;
    LifecycleSnapshot state;
    std::unordered_map<std::uint64_t, std::shared_ptr<Invocation>> invocations;
    std::uint64_t next_id = 1;
    std::mutex runtime_mutex;
    std::condition_variable runtime_cv;
    std::deque<std::function<void()>> runtime_commands;
    bool runtime_closed = false;
    std::thread runtime_thread;
    std::mutex dispatch_mutex;
    std::condition_variable dispatch_cv;
    std::deque<DispatchItem> events;
    bool dispatch_closed = false;
    std::thread dispatch_thread;
};

ModelHost::ModelHost(Options options)
    : impl_(std::make_shared<Impl>(
          std::move(options),
          test::active_model_host_hooks() ? *test::active_model_host_hooks() : test::ModelHostHooks{})) {
}

ModelHost::~ModelHost() {
    if (impl_) impl_->shutdown();
}

OperationResult ModelHost::load(const ModelSpec &model) {
    if (model.id.value.empty()) return {false, {FailureCode::InvalidRequest, "model id is empty"}};
    {
        std::lock_guard lock(impl_->state_mutex);
        if (impl_->state.state != LifecycleState::Unloaded)
            return {false, {FailureCode::LifecycleTransition, "model host is not unloaded"}};
        impl_->state.state = LifecycleState::Loading;
    }
    const auto result = impl_->command([impl = impl_, model] {
        Failure failure;
        if (impl->runtime_failure) {
            failure = impl->runtime_failure;
        } else if (impl->runtime != nullptr) {
            try {
                failure = impl->runtime->load(model);
            } catch (const std::exception &error) {
                failure = {FailureCode::Runtime, error.what()};
            } catch (...) {
                failure = {FailureCode::Runtime, "unknown model load failure"};
            }
        } else {
            failure = {FailureCode::Runtime, "model runtime is unavailable"};
        }
        if (!failure && impl->hooks.load_runtime) {
            try {
                failure = impl->hooks.load_runtime(model);
            } catch (const std::exception &error) {
                failure = {FailureCode::Runtime, error.what()};
            } catch (...) {
                failure = {FailureCode::Runtime, "unknown test runtime load failure"};
            }
        }
        bool shutdown_won = false;
        {
            std::lock_guard lock(impl->state_mutex);
            shutdown_won = impl->state.state != LifecycleState::Loading;
        }
        if (failure || shutdown_won) {
            if (impl->hooks.unload_runtime) {
                try {
                    const Failure cleanup_failure = impl->hooks.unload_runtime();
                    if (!failure && cleanup_failure) failure = cleanup_failure;
                } catch (const std::exception &error) {
                    if (!failure) failure = {FailureCode::Runtime, error.what()};
                } catch (...) {
                    if (!failure) failure = {FailureCode::Runtime, "unknown model cleanup failure"};
                }
            }
            std::lock_guard lock(impl->state_mutex);
            if (impl->state.state == LifecycleState::Loading) impl->state = {};
            return failure ? failure : Failure{FailureCode::Shutdown, "shutdown won model load"};
        }
        std::lock_guard lock(impl->state_mutex);
        impl->state.state = LifecycleState::Ready;
        impl->state.model = model.id;
        // Sole source of truth for whether the loaded model accepts
        // ConversationInput: the resolved prompt profile. Cleared implicitly
        // by the `LifecycleSnapshot{}` reset on unload/load failure.
        impl->state.supports_conversation =
            impl->runtime != nullptr && impl->runtime->profile().supports_conversation;
        return Failure{};
    });
    if (!result) {
        std::lock_guard lock(impl_->state_mutex);
        if (impl_->state.state == LifecycleState::Loading) impl_->state = {};
    }
    return result;
}

OperationResult ModelHost::unload() {
    {
        std::lock_guard lock(impl_->state_mutex);
        if (impl_->state.state != LifecycleState::Ready || impl_->state.active_invocations != 0)
            return {false, {FailureCode::LifecycleTransition, "model host cannot unload now"}};
        impl_->state.state = LifecycleState::Unloading;
    }
    const auto result = impl_->command([impl = impl_] {
        if (impl->hooks.unload_runtime || impl->runtime != nullptr) {
            const Failure failure = impl->hooks.unload_runtime ? impl->hooks.unload_runtime() : impl->runtime->unload();
            if (failure) {
                std::lock_guard lock(impl->state_mutex);
                if (impl->state.state == LifecycleState::Unloading) impl->state.state = LifecycleState::Ready;
                return failure;
            }
        }
        std::lock_guard lock(impl->state_mutex);
        if (impl->state.state != LifecycleState::Unloading)
            return Failure{FailureCode::Shutdown, "shutdown won model unload"};
        impl->state = {};
        return Failure{};
    });
    if (!result) {
        std::lock_guard lock(impl_->state_mutex);
        if (impl_->state.state == LifecycleState::Unloading) impl_->state.state = LifecycleState::Ready;
    }
    return result;
}

SubmitResult ModelHost::submit(const InvocationRequest &request, InvocationEventSink observer) {
    if (request.model.value.empty()) return {false, {}, {FailureCode::InvalidRequest, "model id is empty"}, {}};
    std::lock_guard lock(impl_->state_mutex);
    if (impl_->state.state != LifecycleState::Ready || !impl_->state.model || *impl_->state.model != request.model)
        return {false, {}, {FailureCode::NotLoaded, "requested model is not ready"}, {}};
    {
        std::lock_guard runtime_lock(impl_->runtime_mutex);
        if (impl_->runtime_closed) return {false, {}, {FailureCode::Shutdown, "model host is stopping"}, {}};
        auto invocation = std::make_shared<Impl::Invocation>();
        invocation->request = request;
        invocation->observer = std::move(observer);
        invocation->id = InvocationId{impl_->next_id++};
        impl_->invocations.emplace(invocation->id.value, invocation);
        ++impl_->state.active_invocations;
        if (!impl_->scheduler.push({invocation->id, request.work_class})) {
            impl_->invocations.erase(invocation->id.value);
            --impl_->state.active_invocations;
            return {false, {}, {FailureCode::Backpressure, "model host admission queue is full"}, {}};
        }
        for (const auto &[other_id, other] : impl_->invocations) {
            if ((request.work_class == WorkClass::NativeInteractive || request.work_class == WorkClass::ApiInteractive) &&
                other->request.work_class == WorkClass::Batch && other->running)
                impl_->request_stop(*other, StopReason::Preempted);
        }
        impl_->runtime_commands.emplace_back([impl = impl_] { impl->run_scheduled(); });
        impl_->runtime_cv.notify_one();
        const auto id = invocation->id;
        const std::weak_ptr<Impl> weak_impl = impl_;
        return {true, id, {}, InvocationHandle{id, [weak_impl, id] {
                                                   const auto impl = weak_impl.lock();
                                                   if (!impl) return OperationResult{false, {FailureCode::Shutdown, "model host is stopped"}};
                                                   return impl->cancel_id(id, StopReason::UserCancel);
                                               }}};
    }
}

OperationResult ModelHost::cancel(InvocationId id) {
    return impl_->cancel_id(id, StopReason::UserCancel);
}

OperationResult ModelHost::preempt(InvocationId id) {
    return impl_->cancel_id(id, StopReason::Preempted);
}

LifecycleSnapshot ModelHost::snapshot() const {
    std::lock_guard lock(impl_->state_mutex);
    return impl_->state;
}

OperationResult ModelHost::shutdown() {
    return impl_->shutdown();
}

OperationResult InvocationHandle::cancel() const {
    if (!cancel_function_) return {false, {FailureCode::Shutdown, "model host is stopped"}};
    return cancel_function_();
}

}  // namespace qtrans::core
