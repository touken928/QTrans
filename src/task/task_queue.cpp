#include "task/task_queue.h"

namespace {

bool is_normal_translate(const Task & task) {
    return task.kind == TaskKind::TranslatePipeline
        && task.priority == TaskPriority::Normal;
}

} // namespace

TaskId TaskQueue::enqueue(Task task) {
    std::lock_guard<std::mutex> lock(mutex_);

    task.id.value = next_id_++;
    task.state = TaskState::Pending;
    states_[task.id.value] = TaskState::Pending;

    const TaskId id = task.id;

    if (task.priority == TaskPriority::Interactive) {
        pending_.push_front(std::move(task));
    } else {
        pending_.push_back(std::move(task));
    }

    return id;
}

std::optional<Task> TaskQueue::pop_next() {
    std::lock_guard<std::mutex> lock(mutex_);

    while (!pending_.empty()) {
        Task task = std::move(pending_.front());
        pending_.pop_front();

        const TaskState current = states_[task.id.value];
        if (current == TaskState::Cancelled) {
            continue;
        }

        task.state = TaskState::Running;
        states_[task.id.value] = TaskState::Running;
        return task;
    }

    return std::nullopt;
}

bool TaskQueue::cancel(TaskId id) {
    if (!id.is_valid()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const auto state_it = states_.find(id.value);
    if (state_it == states_.end()) {
        return false;
    }

    if (state_it->second == TaskState::Pending) {
        state_it->second = TaskState::Cancelled;
        return true;
    }

    return state_it->second == TaskState::Running;
}

void TaskQueue::cancel_pending_normal_translate_tasks() {
    (void)cancel_all_pending_normal_translate_tasks();
}

std::vector<TaskId> TaskQueue::cancel_all_pending_normal_translate_tasks() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<TaskId> cancelled;
    for (Task & task : pending_) {
        if (is_normal_translate(task) && states_[task.id.value] == TaskState::Pending) {
            states_[task.id.value] = TaskState::Cancelled;
            cancelled.push_back(task.id);
        }
    }
    return cancelled;
}

TaskState TaskQueue::state_of(TaskId id) const {
    if (!id.is_valid()) {
        return TaskState::Failed;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = states_.find(id.value);
    if (it == states_.end()) {
        return TaskState::Failed;
    }
    return it->second;
}

void TaskQueue::set_state(TaskId id, TaskState state) {
    if (!id.is_valid()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    states_[id.value] = state;
}

bool TaskQueue::has_pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !pending_.empty();
}

bool TaskQueue::empty() const {
    return !has_pending();
}
