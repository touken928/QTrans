#include "domain/tasks/task_queue.h"

namespace {

bool is_normal_translate(const Task &task) {
    return task.kind == TaskKind::TranslatePipeline && task.priority == TaskPriority::Normal;
}

}  // namespace

TaskId TaskQueue::enqueue(Task task) {
    std::lock_guard<std::mutex> lock(mutex_);

    task.id.value = next_id_++;
    task.state = TaskState::Pending;
    states_[task.id.value] = TaskState::Pending;
    kinds_[task.id.value] = task.kind;
    priorities_[task.id.value] = task.priority;

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

TaskQueue::CancelResult TaskQueue::cancel(TaskId id, CancellationReason reason) {
    if (!id.is_valid()) return CancelResult::NotFound;

    std::lock_guard<std::mutex> lock(mutex_);

    const auto state_it = states_.find(id.value);
    if (state_it == states_.end()) {
        return CancelResult::NotFound;
    }

    if (state_it->second == TaskState::Pending) {
        state_it->second = TaskState::Cancelled;
        return CancelResult::PendingCancelled;
    }

    if (state_it->second == TaskState::Running) {
        const auto kind_it = kinds_.find(id.value);
        if (kind_it != kinds_.end() &&
            (kind_it->second == TaskKind::LoadModel || kind_it->second == TaskKind::UnloadModel)) {
            return CancelResult::NotCancellable;
        }
        const auto existing = cancellation_reasons_.find(id.value);
        const CancellationReason current = existing == cancellation_reasons_.end()
                                               ? CancellationReason::None
                                               : existing->second;
        cancellation_reasons_[id.value] = merge_cancellation_reason(current, reason);
        return CancelResult::AlreadyRunning;
    }
    return CancelResult::AlreadyTerminal;
}

std::optional<CancellationReason> TaskQueue::cancellation_reason(TaskId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = cancellation_reasons_.find(id.value);
    if (it == cancellation_reasons_.end()) return std::nullopt;
    return it->second;
}

void TaskQueue::cancel_pending_normal_translate_tasks() {
    (void)cancel_all_pending_normal_translate_tasks();
}

std::vector<TaskId> TaskQueue::cancel_all_pending_normal_translate_tasks() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<TaskId> cancelled;
    for (Task &task : pending_) {
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

TaskKind TaskQueue::kind_of(TaskId id) const {
    if (!id.is_valid()) return TaskKind::TranslatePipeline;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = kinds_.find(id.value);
    return it == kinds_.end() ? TaskKind::TranslatePipeline : it->second;
}

TaskQueue::SettlementResult TaskQueue::settle_running(TaskId id, TaskState requested_state) {
    if (!id.is_valid()) return {};

    std::lock_guard<std::mutex> lock(mutex_);
    const auto state_it = states_.find(id.value);
    if (state_it == states_.end() || state_it->second != TaskState::Running) {
        return {false, state_it == states_.end() ? TaskState::Failed : state_it->second, CancellationReason::None};
    }

    const auto reason_it = cancellation_reasons_.find(id.value);
    const CancellationReason reason = reason_it == cancellation_reasons_.end()
                                          ? CancellationReason::None
                                          : reason_it->second;
    const bool background_translation = kinds_[id.value] == TaskKind::TranslatePipeline &&
                                        priorities_[id.value] == TaskPriority::Background;
    const TaskState final_state = reason == CancellationReason::None
                                      ? requested_state
                                      : (background_translation && is_preemption_reason(reason)
                                             ? TaskState::Preempted
                                             : TaskState::Cancelled);
    state_it->second = final_state;
    cancellation_reasons_.erase(id.value);
    return {true, final_state, reason};
}

void TaskQueue::set_state(TaskId id, TaskState state) {
    if (!id.is_valid()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    states_[id.value] = state;
    if (state != TaskState::Running) cancellation_reasons_.erase(id.value);
}

bool TaskQueue::has_pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !pending_.empty();
}

bool TaskQueue::empty() const {
    return !has_pending();
}
