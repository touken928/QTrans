#include "task/task_queue.h"
#include "task/task_types.h"

#include <gtest/gtest.h>

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
    DownloadSpec spec;
    spec.repo = "Tencent-Hunyuan/Hy-MT2-1.8B-GGUF";
    spec.filename = "Hy-MT2-1.8B-Q4_K_M.gguf";
    task.payload = DownloadModelPayload{"/tmp/model.gguf", spec};
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
    EXPECT_TRUE(queue.cancel(id));
    EXPECT_EQ(queue.state_of(id), TaskState::Cancelled);

    auto task = queue.pop_next();
    EXPECT_FALSE(task.has_value());
}

TEST(TaskQueue, CancelRunningAffectsRunning) {
    TaskQueue queue;
    const TaskId id = queue.enqueue(make_translate(TaskPriority::Normal));
    ASSERT_TRUE(queue.pop_next().has_value());
    EXPECT_TRUE(queue.cancel(id));
    // Queue::cancel() reports the running task is affected but does not
    // transition the state itself; the orchestrator flips the state via
    // set_state() after the running task acknowledges cancellation.
    EXPECT_EQ(queue.state_of(id), TaskState::Running);
}

TEST(TaskQueue, CancelUnknownOrInvalidFails) {
    TaskQueue queue;
    EXPECT_FALSE(queue.cancel(TaskId{0}));
    EXPECT_FALSE(queue.cancel(TaskId{42}));
    queue.enqueue(make_translate(TaskPriority::Normal));
    EXPECT_FALSE(queue.cancel(TaskId{9999}));
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
