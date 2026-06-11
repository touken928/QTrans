#pragma once

#include "task/task_types.h"

#include <deque>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

class TaskQueue {
public:
    TaskId enqueue(Task task);
    std::optional<Task> pop_next();

    bool cancel(TaskId id);
    void cancel_pending_normal_translate_tasks();
    std::vector<TaskId> cancel_all_pending_normal_translate_tasks();

    TaskState state_of(TaskId id) const;
    void set_state(TaskId id, TaskState state);
    bool has_pending() const;
    bool empty() const;

private:
    mutable std::mutex mutex_;
    std::deque<Task> pending_;
    std::unordered_map<std::uint64_t, TaskState> states_;
    std::uint64_t next_id_ = 1;
};
