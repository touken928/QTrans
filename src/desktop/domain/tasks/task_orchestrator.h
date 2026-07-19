#pragma once

#include "domain/tasks/task_execution.h"
#include "domain/tasks/task_queue.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>

struct TaskOrchestratorCallbacks {
    std::function<void(const std::string &, bool)> on_status;
    std::function<void(bool, const std::string &)> on_model_load_finished;
    std::function<void()> on_model_unload_finished;
    std::function<void(std::int64_t, std::int64_t, double, double)> on_download_progress;
    std::function<void(bool)> on_download_finished;
    std::function<void(std::uint64_t, TaskState)> on_task_state_changed;
    std::function<void(std::uint64_t)> on_target_reset;
    std::function<void(std::uint64_t, const std::string &)> on_target_appended;
    std::function<void(std::uint64_t)> on_back_translate_reset;
    std::function<void(std::uint64_t, const std::string &)> on_back_translate_appended;
    std::function<void(std::uint64_t, TaskState)> on_translation_finished;
    std::function<void(std::uint64_t, const std::string &)> on_task_failed;
};

class TaskOrchestrator {
public:
    explicit TaskOrchestrator(TaskExecutors executors);

    void set_callbacks(TaskOrchestratorCallbacks callbacks);
    void set_model_path(const std::string &path);
    void set_model_id(const std::string &id);
    void initialize_backend();
    void set_remote_spec(const std::string &spec);
    void set_modelscope_remote_spec(const std::string &spec);
    void set_download_hub(int hub);
    std::string active_backend_label() const;

    TaskId submit_download_model(TaskPriority priority = TaskPriority::Interactive);
    TaskId submit_load_model(TaskPriority priority = TaskPriority::Interactive);
    TaskId submit_unload_model(TaskPriority priority = TaskPriority::Interactive);
    TaskId submit_translate_pipeline(const TranslatePipelinePayload &, TaskPriority = TaskPriority::Interactive);
    bool cancel(TaskId id);
    bool cancel_running();
    bool preempt_running_background();
    TaskState task_state(TaskId id) const;
    bool is_model_loaded() const;
    void process_next();

private:
    void apply_interactive_preemption(const Task &incoming_task);
    void execute_task(Task task);
    TaskQueue::SettlementResult finalize_task(TaskId id, TaskKind kind, TaskState requested_state);
    void emit_finalized(TaskId id, TaskKind kind, TaskState state);
    void emit_status(const std::string &message, bool busy) const;

    mutable std::mutex mutex_;
    TaskQueue queue_;
    TaskExecutors executors_;
    TaskOrchestratorCallbacks callbacks_;
    std::string model_path_;
    std::string model_id_;
    std::string remote_spec_ = "tencent/Hy-MT2-1.8B-GGUF/Hy-MT2-1.8B-Q4_K_M.gguf";
    std::string modelscope_remote_spec_ = "Tencent-Hunyuan/Hy-MT2-1.8B-GGUF/Hy-MT2-1.8B-Q4_K_M.gguf";
    int download_hub_ = 2;
    TaskId running_task_id_{};
    TaskPriority running_priority_ = TaskPriority::Normal;
    std::shared_ptr<CancelToken> running_cancel_token_;
    bool processing_ = false;
    bool model_loaded_ = false;
};
