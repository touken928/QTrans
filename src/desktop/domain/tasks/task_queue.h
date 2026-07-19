#pragma once

#include "domain/tasks/task_execution.h"

#include <deque>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

class TaskQueue {
public:
    enum class CancelResult {
        PendingCancelled,
        AlreadyRunning,
        NotCancellable,
        AlreadyTerminal,
        NotFound,
    };

    struct SettlementResult {
        bool committed = false;
        TaskState state = TaskState::Failed;
        CancellationReason reason = CancellationReason::None;
    };

    TaskId enqueue(Task task);
    std::optional<Task> pop_next();

    CancelResult cancel(TaskId id, CancellationReason reason = CancellationReason::User);
    std::optional<CancellationReason> cancellation_reason(TaskId id) const;
    void cancel_pending_normal_translate_tasks();
    std::vector<TaskId> cancel_all_pending_normal_translate_tasks();

    TaskState state_of(TaskId id) const;
    TaskKind kind_of(TaskId id) const;
    SettlementResult settle_running(TaskId id, TaskState requested_state);
    void set_state(TaskId id, TaskState state);
    bool has_pending() const;
    bool empty() const;

private:
    mutable std::mutex mutex_;
    std::deque<Task> pending_;
    std::unordered_map<std::uint64_t, TaskState> states_;
    std::unordered_map<std::uint64_t, TaskKind> kinds_;
    std::unordered_map<std::uint64_t, TaskPriority> priorities_;
    std::unordered_map<std::uint64_t, CancellationReason> cancellation_reasons_;
    std::uint64_t next_id_ = 1;
};
