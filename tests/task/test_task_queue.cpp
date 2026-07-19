#include "domain/tasks/task_queue.h"
#include "domain/tasks/task_types.h"

#include <gtest/gtest.h>

#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
namespace {

Task make_translate(TaskPriority priority, std::string source = "hello") {
    Task task;
    task.kind = TaskKind::TranslatePipeline;
    task.priority = priority;
    task.payload = TranslatePipelinePayload{std::move(source), "Chinese", "English", false};
    return task;
}

Task make_download() {
    Task task;
    task.kind = TaskKind::DownloadModel;
    task.priority = TaskPriority::Normal;
    task.payload = DownloadModelPayload{
        "/tmp/model.gguf",
        "Tencent-Hunyuan/Hy-MT2-1.8B-GGUF/Hy-MT2-1.8B-Q4_K_M.gguf",
        "",
        2};
    return task;
}

}  // namespace

TEST(TaskQueue, EnqueueReturnsMonotonicIds) {
    TaskQueue queue;
    const TaskId a = queue.enqueue(make_translate(TaskPriority::Normal));
    const TaskId b = queue.enqueue(make_translate(TaskPriority::Normal));
    EXPECT_TRUE(a.is_valid());
    EXPECT_TRUE(b.is_valid());
    EXPECT_NE(a, b);
    EXPECT_EQ(queue.state_of(a), TaskState::Pending);
    EXPECT_EQ(queue.state_of(b), TaskState::Pending);
}

TEST(TaskQueue, InvalidTaskIdStateIsFailed) {
    TaskQueue queue;
    EXPECT_EQ(queue.state_of(TaskId{0}), TaskState::Failed);
    EXPECT_EQ(queue.state_of(TaskId{999}), TaskState::Failed);
}

TEST(TaskQueue, PopNextOnEmptyReturnsNullopt) {
    TaskQueue queue;
    EXPECT_FALSE(queue.pop_next().has_value());
    EXPECT_FALSE(queue.has_pending());
    EXPECT_TRUE(queue.empty());
}

TEST(TaskQueue, PopNextMarksRunning) {
    TaskQueue queue;
    const TaskId id = queue.enqueue(make_translate(TaskPriority::Normal));

    auto task = queue.pop_next();
    ASSERT_TRUE(task.has_value());
    EXPECT_EQ(task->id, id);
    EXPECT_EQ(task->state, TaskState::Running);
    EXPECT_EQ(queue.state_of(id), TaskState::Running);
    EXPECT_FALSE(queue.has_pending());
}

TEST(TaskQueue, InteractivePriorityIsLifo) {
    TaskQueue queue;
    const TaskId a = queue.enqueue(make_translate(TaskPriority::Normal, "first"));
    const TaskId b = queue.enqueue(make_translate(TaskPriority::Interactive, "second"));

    auto first = queue.pop_next();
    auto second = queue.pop_next();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->id, b);
    EXPECT_EQ(second->id, a);
}

TEST(TaskQueue, NormalPriorityIsFifo) {
    TaskQueue queue;
    const TaskId a = queue.enqueue(make_translate(TaskPriority::Normal, "first"));
    const TaskId b = queue.enqueue(make_translate(TaskPriority::Normal, "second"));

    auto first = queue.pop_next();
    auto second = queue.pop_next();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->id, a);
    EXPECT_EQ(second->id, b);
}

TEST(TaskQueue, CancelPendingBeforePopSucceeds) {
    TaskQueue queue;
    const TaskId id = queue.enqueue(make_translate(TaskPriority::Normal));
    EXPECT_EQ(queue.cancel(id), TaskQueue::CancelResult::PendingCancelled);
    EXPECT_EQ(queue.state_of(id), TaskState::Cancelled);

    auto task = queue.pop_next();
    EXPECT_FALSE(task.has_value());
}

TEST(TaskQueue, CancelRunningAffectsRunning) {
    TaskQueue queue;
    const TaskId id = queue.enqueue(make_translate(TaskPriority::Normal));
    ASSERT_TRUE(queue.pop_next().has_value());
    EXPECT_EQ(queue.cancel(id), TaskQueue::CancelResult::AlreadyRunning);
    // Queue::cancel() reports the running task is affected but does not
    // transition the state itself; the orchestrator flips the state via
    // set_state() after the running task acknowledges cancellation.
    EXPECT_EQ(queue.state_of(id), TaskState::Running);
}

TEST(TaskQueue, RunningCancellationIsObservedByWorker) {
    TaskQueue queue;
    const TaskId id = queue.enqueue(make_translate(TaskPriority::Normal));
    ASSERT_TRUE(queue.pop_next().has_value());

    EXPECT_EQ(queue.cancel(id), TaskQueue::CancelResult::AlreadyRunning);
    ASSERT_TRUE(queue.cancellation_reason(id).has_value());
    EXPECT_EQ(*queue.cancellation_reason(id), CancellationReason::User);
    EXPECT_EQ(queue.state_of(id), TaskState::Running);

    queue.set_state(id, TaskState::Cancelled);
    EXPECT_FALSE(queue.cancellation_reason(id).has_value());
}

TEST(TaskQueue, CancellationReasonPublishesWithRunningRequest) {
    TaskQueue queue;
    const TaskId id = queue.enqueue(make_translate(TaskPriority::Background));
    ASSERT_TRUE(queue.pop_next().has_value());

    std::mutex mutex;
    std::condition_variable condition;
    bool ready = false;
    std::optional<CancellationReason> observed;
    std::thread worker([&] {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return ready; });
        observed = queue.cancellation_reason(id);
    });

    EXPECT_EQ(queue.cancel(id, CancellationReason::Pause), TaskQueue::CancelResult::AlreadyRunning);
    {
        std::lock_guard<std::mutex> lock(mutex);
        ready = true;
    }
    condition.notify_one();
    worker.join();

    ASSERT_TRUE(observed.has_value());
    EXPECT_EQ(*observed, CancellationReason::Pause);
}

TEST(TaskQueue, ConcurrentUserCancellationCannotDowngradePreemption) {
    TaskQueue queue;
    const TaskId id = queue.enqueue(make_translate(TaskPriority::Background));
    ASSERT_TRUE(queue.pop_next().has_value());

    std::mutex mutex;
    std::condition_variable condition;
    int arrived = 0;
    bool go = false;
    auto cancel_when_released = [&](CancellationReason reason) {
        std::unique_lock<std::mutex> lock(mutex);
        ++arrived;
        condition.notify_all();
        condition.wait(lock, [&] { return go; });
        lock.unlock();
        queue.cancel(id, reason);
    };
    std::thread pause_thread(cancel_when_released, CancellationReason::Pause);
    std::thread user_thread(cancel_when_released, CancellationReason::User);
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return arrived == 2; });
        go = true;
    }
    condition.notify_all();
    pause_thread.join();
    user_thread.join();

    ASSERT_TRUE(queue.cancellation_reason(id).has_value());
    EXPECT_TRUE(is_preemption_reason(*queue.cancellation_reason(id)));
}

TEST(TaskQueue, SettlementRechecksReasonBeforeLinearizingTerminalState) {
    TaskQueue queue;
    const TaskId id = queue.enqueue(make_translate(TaskPriority::Background));
    ASSERT_TRUE(queue.pop_next().has_value());
    ASSERT_EQ(queue.cancel(id, CancellationReason::User), TaskQueue::CancelResult::AlreadyRunning);

    const auto worker_snapshot = queue.cancellation_reason(id);
    ASSERT_TRUE(worker_snapshot.has_value());
    EXPECT_EQ(*worker_snapshot, CancellationReason::User);

    std::mutex mutex;
    std::condition_variable condition;
    bool continue_worker = false;
    TaskQueue::SettlementResult settlement;
    std::thread worker([&] {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return continue_worker; });
        lock.unlock();
        settlement = queue.settle_running(id, TaskState::Cancelled);
    });

    ASSERT_EQ(queue.cancel(id, CancellationReason::Pause), TaskQueue::CancelResult::AlreadyRunning);
    {
        std::lock_guard<std::mutex> lock(mutex);
        continue_worker = true;
    }
    condition.notify_one();
    worker.join();

    ASSERT_TRUE(settlement.committed);
    EXPECT_EQ(settlement.state, TaskState::Preempted);
    EXPECT_EQ(queue.state_of(id), TaskState::Preempted);
    EXPECT_FALSE(queue.cancellation_reason(id).has_value());
    EXPECT_FALSE(queue.settle_running(id, TaskState::Completed).committed);
    EXPECT_EQ(queue.cancel(id, CancellationReason::User), TaskQueue::CancelResult::AlreadyTerminal);
    EXPECT_EQ(queue.state_of(id), TaskState::Preempted);
}

TEST(TaskQueue, CancelUnknownOrInvalidFails) {
    TaskQueue queue;
    EXPECT_EQ(queue.cancel(TaskId{0}), TaskQueue::CancelResult::NotFound);
    EXPECT_EQ(queue.cancel(TaskId{42}), TaskQueue::CancelResult::NotFound);
    queue.enqueue(make_translate(TaskPriority::Normal));
    EXPECT_EQ(queue.cancel(TaskId{9999}), TaskQueue::CancelResult::NotFound);
}

TEST(TaskQueue, SetStateOverridesState) {
    TaskQueue queue;
    const TaskId id = queue.enqueue(make_translate(TaskPriority::Normal));
    queue.set_state(id, TaskState::Completed);
    EXPECT_EQ(queue.state_of(id), TaskState::Completed);
}

TEST(TaskQueue, SetStateInvalidIdIsNoop) {
    TaskQueue queue;
    queue.set_state(TaskId{0}, TaskState::Completed);  // must not throw
}

TEST(TaskQueue, CancelAllPendingNormalTranslates) {
    TaskQueue queue;
    const TaskId t1 = queue.enqueue(make_translate(TaskPriority::Normal, "a"));
    const TaskId download = queue.enqueue(make_download());
    const TaskId t2 = queue.enqueue(make_translate(TaskPriority::Normal, "b"));
    const TaskId interactive = queue.enqueue(make_translate(TaskPriority::Interactive, "c"));

    const auto cancelled = queue.cancel_all_pending_normal_translate_tasks();
    ASSERT_EQ(cancelled.size(), 2u);
    EXPECT_EQ(cancelled[0], t1);
    EXPECT_EQ(cancelled[1], t2);
    EXPECT_EQ(queue.state_of(t1), TaskState::Cancelled);
    EXPECT_EQ(queue.state_of(t2), TaskState::Cancelled);
    EXPECT_EQ(queue.state_of(download), TaskState::Pending);
    EXPECT_EQ(queue.state_of(interactive), TaskState::Pending);

    // Interactive is at the front of the deque; download and the cancelled
    // translates follow. pop_next() returns interactive and download, then
    // skips the two cancelled translates.
    auto first = queue.pop_next();
    auto second = queue.pop_next();
    auto third = queue.pop_next();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->id, interactive);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->id, download);
    EXPECT_FALSE(third.has_value());
}

TEST(TaskQueue, CancelPendingNormalTranslatesVoidOverload) {
    TaskQueue queue;
    queue.enqueue(make_translate(TaskPriority::Normal, "a"));
    queue.enqueue(make_translate(TaskPriority::Normal, "b"));
    queue.cancel_pending_normal_translate_tasks();  // must not throw
    EXPECT_FALSE(queue.pop_next().has_value());
}
