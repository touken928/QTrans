#include "domain/tasks/task_orchestrator.h"

#include <exception>
#include <filesystem>
#include <utility>

TaskOrchestrator::TaskOrchestrator(TaskExecutors executors)
    : executors_(std::move(executors)) {
}

void TaskOrchestrator::set_callbacks(TaskOrchestratorCallbacks callbacks) {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_ = std::move(callbacks);
}
void TaskOrchestrator::set_model_path(const std::string &path) {
    std::lock_guard<std::mutex> lock(mutex_);
    model_path_ = path;
}
void TaskOrchestrator::set_model_id(const std::string &id) {
    std::lock_guard<std::mutex> lock(mutex_);
    model_id_ = id;
}
void TaskOrchestrator::initialize_backend() {
    executors_.translation_session->initialize_backend();
}
void TaskOrchestrator::set_remote_spec(const std::string &spec) {
    std::lock_guard<std::mutex> lock(mutex_);
    remote_spec_ = spec;
}
void TaskOrchestrator::set_modelscope_remote_spec(const std::string &spec) {
    std::lock_guard<std::mutex> lock(mutex_);
    modelscope_remote_spec_ = spec;
}
void TaskOrchestrator::set_download_hub(int hub) {
    std::lock_guard<std::mutex> lock(mutex_);
    download_hub_ = hub;
}
std::string TaskOrchestrator::active_backend_label() const {
    return executors_.translation_session->active_backend_label();
}

TaskId TaskOrchestrator::submit_download_model(TaskPriority priority) {
    DownloadModelPayload payload;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        payload = {model_path_, remote_spec_, modelscope_remote_spec_, download_hub_};
    }
    Task task{};
    task.kind = TaskKind::DownloadModel;
    task.priority = priority;
    task.payload = std::move(payload);
    return queue_.enqueue(std::move(task));
}
TaskId TaskOrchestrator::submit_load_model(TaskPriority priority) {
    LoadModelPayload payload;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        payload = {model_id_, std::filesystem::u8path(model_path_)};
    }
    Task task{};
    task.kind = TaskKind::LoadModel;
    task.priority = priority;
    task.payload = std::move(payload);
    return queue_.enqueue(std::move(task));
}
TaskId TaskOrchestrator::submit_unload_model(TaskPriority priority) {
    Task task{};
    task.kind = TaskKind::UnloadModel;
    task.priority = priority;
    task.payload = LoadModelPayload{};
    return queue_.enqueue(std::move(task));
}
TaskId TaskOrchestrator::submit_translate_pipeline(const TranslatePipelinePayload &payload, TaskPriority priority) {
    Task task{};
    task.kind = TaskKind::TranslatePipeline;
    task.priority = priority;
    task.payload = payload;
    apply_interactive_preemption(task);
    return queue_.enqueue(std::move(task));
}

void TaskOrchestrator::apply_interactive_preemption(const Task &incoming) {
    if (incoming.kind != TaskKind::TranslatePipeline || incoming.priority != TaskPriority::Interactive) return;
    for (const TaskId id : queue_.cancel_all_pending_normal_translate_tasks()) {
        emit_finalized(id, TaskKind::TranslatePipeline, TaskState::Cancelled);
    }
    TaskId running_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!processing_ || !running_task_id_.is_valid() ||
            (running_priority_ != TaskPriority::Normal && running_priority_ != TaskPriority::Background)) {
            return;
        }
        running_id = running_task_id_;
    }
    if (queue_.cancel(running_id, CancellationReason::Interactive) != TaskQueue::CancelResult::AlreadyRunning) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_task_id_ == running_id && running_cancel_token_) {
        running_cancel_token_->cancel();
    }
}

bool TaskOrchestrator::cancel(TaskId id) {
    if (!id.is_valid()) return false;
    const TaskQueue::CancelResult result = queue_.cancel(id, CancellationReason::User);
    if (result == TaskQueue::CancelResult::PendingCancelled) {
        emit_finalized(id, queue_.kind_of(id), TaskState::Cancelled);
        return true;
    }
    if (result == TaskQueue::CancelResult::AlreadyRunning) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_task_id_ == id && running_cancel_token_) {
            running_cancel_token_->cancel();
        }
        return true;
    }
    if (result == TaskQueue::CancelResult::NotCancellable) return false;
    return queue_.state_of(id) == TaskState::Cancelled;
}
bool TaskOrchestrator::cancel_running() {
    TaskId id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!processing_ || !running_task_id_.is_valid()) return false;
        id = running_task_id_;
    }
    return cancel(id);
}
bool TaskOrchestrator::preempt_running_background() {
    TaskId id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!processing_ || running_priority_ != TaskPriority::Background || !running_task_id_.is_valid()) return false;
        id = running_task_id_;
    }
    const TaskQueue::CancelResult result = queue_.cancel(id, CancellationReason::Pause);
    if (result != TaskQueue::CancelResult::AlreadyRunning) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_task_id_ == id && running_cancel_token_) {
        running_cancel_token_->cancel();
    }
    return true;
}
TaskState TaskOrchestrator::task_state(TaskId id) const {
    return queue_.state_of(id);
}
bool TaskOrchestrator::is_model_loaded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return model_loaded_ && executors_.translation_session->is_loaded();
}

void TaskOrchestrator::emit_finalized(TaskId id, TaskKind kind, TaskState state) {
    if (callbacks_.on_task_state_changed) callbacks_.on_task_state_changed(id.value, state);
    if (kind == TaskKind::TranslatePipeline && callbacks_.on_translation_finished) callbacks_.on_translation_finished(id.value, state);
}
TaskQueue::SettlementResult TaskOrchestrator::finalize_task(
    TaskId id,
    TaskKind kind,
    TaskState requested_state) {
    const TaskQueue::SettlementResult result = queue_.settle_running(id, requested_state);
    if (result.committed) emit_finalized(id, kind, result.state);
    return result;
}
void TaskOrchestrator::emit_status(const std::string &message, bool busy) const {
    if (callbacks_.on_status) callbacks_.on_status(message, busy);
}

void TaskOrchestrator::process_next() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (processing_) return;
        processing_ = true;
    }
    while (const auto task = queue_.pop_next()) execute_task(*task);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        processing_ = false;
        running_task_id_ = {};
        running_cancel_token_.reset();
    }
    emit_status("Ready", false);
}

void TaskOrchestrator::execute_task(Task task) {
    const TaskId id = task.id;
    auto token = std::make_shared<CancelToken>();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_task_id_ = id;
        running_priority_ = task.priority;
        running_cancel_token_ = token;
    }
    if (const auto reason = queue_.cancellation_reason(id)) {
        token->cancel();
    }
    if (callbacks_.on_task_state_changed) callbacks_.on_task_state_changed(id.value, TaskState::Running);
    TaskState final_state = TaskState::Failed;
    ExecutionResult execution_result{};
    const auto clear_model = [this]() {
        try {
            (void)executors_.translation_session->unload();
        } catch (...) {
        }
        std::lock_guard<std::mutex> lock(mutex_);
        model_loaded_ = false;
    };
    try {
        ExecutionResult result;
        if (task.kind == TaskKind::DownloadModel) {
            emit_status("Downloading model", true);
            execution_result = executors_.downloader->download(
                std::get<DownloadModelPayload>(task.payload),
                token.get(),
                [this](const DownloadProgressData &p) {
                    if (callbacks_.on_download_progress) {
                        callbacks_.on_download_progress(
                            p.downloaded_bytes, p.total_bytes, p.speed_bps, p.eta_seconds);
                    }
                });
        } else if (task.kind == TaskKind::LoadModel) {
            emit_status("Loading model into memory", true);
            execution_result = executors_.translation_session->load(std::get<LoadModelPayload>(task.payload));
            if (execution_result.outcome == ExecutionOutcome::Completed) {
                std::lock_guard<std::mutex> lock(mutex_);
                model_loaded_ = true;
            } else {
                clear_model();
            }
        } else if (task.kind == TaskKind::UnloadModel) {
            emit_status("Unloading model", true);
            execution_result = executors_.translation_session->unload();
            if (execution_result.outcome == ExecutionOutcome::Completed) {
                std::lock_guard<std::mutex> lock(mutex_);
                model_loaded_ = false;
            }
        } else {
            emit_status("Translating", true);
            execution_result = executors_.translation_session->translate(
                std::get<TranslatePipelinePayload>(task.payload),
                [this, id](bool back) {
                    if (back ? callbacks_.on_back_translate_reset : callbacks_.on_target_reset) {
                        (back ? callbacks_.on_back_translate_reset : callbacks_.on_target_reset)(id.value);
                    }
                },
                [this, id](bool back, const std::string &piece) {
                    if (back ? callbacks_.on_back_translate_appended : callbacks_.on_target_appended) {
                        (back ? callbacks_.on_back_translate_appended : callbacks_.on_target_appended)(id.value, piece);
                    }
                },
                token.get());
        }
        if (execution_result.outcome == ExecutionOutcome::Completed)
            final_state = TaskState::Completed;
        else if (execution_result.outcome == ExecutionOutcome::Cancelled) {
            final_state = TaskState::Cancelled;
        }
    } catch (const std::exception &ex) {
        if (task.kind == TaskKind::LoadModel) {
            clear_model();
        }
        execution_result = {ExecutionOutcome::Failed, ex.what()};
    } catch (...) {
        if (task.kind == TaskKind::LoadModel) {
            clear_model();
        }
        execution_result = {ExecutionOutcome::Failed, "task execution failed"};
    }
    const TaskQueue::SettlementResult settlement = finalize_task(id, task.kind, final_state);
    if (!settlement.committed) return;

    if (task.kind == TaskKind::DownloadModel) {
        if (callbacks_.on_download_finished) {
            callbacks_.on_download_finished(
                execution_result.outcome == ExecutionOutcome::Completed && settlement.state == TaskState::Completed);
        }
        if (settlement.state == TaskState::Failed) {
            emit_status(std::string("Error: ") + execution_result.error_message, false);
        }
    } else if (task.kind == TaskKind::LoadModel) {
        if (callbacks_.on_model_load_finished) {
            callbacks_.on_model_load_finished(
                execution_result.outcome == ExecutionOutcome::Completed && settlement.state == TaskState::Completed,
                execution_result.error_message);
        }
        if (settlement.state == TaskState::Failed) {
            emit_status(std::string("Error: ") + execution_result.error_message, false);
        }
    } else if (task.kind == TaskKind::UnloadModel) {
        if (execution_result.outcome == ExecutionOutcome::Completed && settlement.state == TaskState::Completed &&
            callbacks_.on_model_unload_finished) {
            callbacks_.on_model_unload_finished();
        }
    } else if (settlement.state == TaskState::Failed) {
        emit_status(std::string("Error: ") + execution_result.error_message, false);
        if (execution_result.outcome == ExecutionOutcome::Failed && callbacks_.on_task_failed) {
            callbacks_.on_task_failed(id.value, execution_result.error_message);
        }
    }
}
