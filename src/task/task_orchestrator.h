#pragma once

#include "network/download.h"
#include "task/cancel_token.h"
#include "task/task_queue.h"
#include "task/task_types.h"
#include "translation/inference_engine.h"
#include "translation/translation_model.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>

struct TaskOrchestratorCallbacks {
    std::function<void(const std::string & message, bool busy)> on_status;
    std::function<void(bool success)> on_model_load_finished;
    std::function<void()> on_model_unload_finished;
    std::function<void(
        std::int64_t downloaded_bytes,
        std::int64_t total_bytes,
        double speed_bps,
        double eta_seconds)> on_download_progress;
    std::function<void(bool success)> on_download_finished;
    std::function<void(std::uint64_t task_id, TaskState state)> on_task_state_changed;
    std::function<void(std::uint64_t task_id)> on_target_reset;
    std::function<void(std::uint64_t task_id, const std::string & piece)> on_target_appended;
    std::function<void(std::uint64_t task_id)> on_back_translate_reset;
    std::function<void(std::uint64_t task_id, const std::string & piece)> on_back_translate_appended;
    std::function<void(std::uint64_t task_id, TaskState state)> on_translation_finished;
};

class TaskOrchestrator {
public:
    void set_callbacks(TaskOrchestratorCallbacks callbacks);

    void set_model_path(const std::string & path);
    void set_remote_spec(const std::string & spec);
    void set_download_hub(int hub);

    TaskId submit_download_model(TaskPriority priority = TaskPriority::Interactive);
    TaskId submit_load_model(TaskPriority priority = TaskPriority::Interactive);
    TaskId submit_unload_model(TaskPriority priority = TaskPriority::Interactive);
    TaskId submit_translate_pipeline(
        const TranslatePipelinePayload & payload,
        TaskPriority priority = TaskPriority::Interactive);

    // Thread-safe: may be called while process_next() is blocked in inference.
    bool cancel(TaskId id);
    bool cancel_running();
    TaskState task_state(TaskId id) const;
    bool is_model_loaded() const;

    void process_next();

private:
    DownloadSpec make_download_spec_unlocked() const;
    void finalize_task(TaskId id, TaskKind kind, TaskState state);
    void apply_interactive_preemption(const Task & incoming_task);
    void execute_task(Task task);
    void emit_status(const std::string & message, bool busy) const;

    mutable std::mutex mutex_;
    TaskQueue queue_;
    InferenceEngine engine_;
    TaskOrchestratorCallbacks callbacks_;

    std::string model_path_;
    std::string remote_spec_ = "AngelSlim/Hy-MT2-1.8B-1.25Bit-GGUF/Hy-MT2-1.8B-1.25Bit.gguf";
    int download_hub_ = 2;
    TranslationModelConfig model_config_{};

    TaskId running_task_id_{};
    TaskPriority running_priority_ = TaskPriority::Normal;
    std::shared_ptr<CancelToken> running_cancel_token_;
    bool processing_ = false;
    bool model_loaded_ = false;
};
