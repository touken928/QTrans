#include "task/task_orchestrator.h"

#include "network/download.h"

#include <cstdio>
#include <stdexcept>

namespace {

LoadModelPayload make_load_payload(std::string model_path) {
    LoadModelPayload payload;
    payload.model_path = std::move(model_path);
    return payload;
}

}  // namespace

void TaskOrchestrator::set_callbacks(TaskOrchestratorCallbacks callbacks) {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_ = std::move(callbacks);
}

void TaskOrchestrator::set_model_path(const std::string &path) {
    std::lock_guard<std::mutex> lock(mutex_);
    model_path_ = path;
}

void TaskOrchestrator::set_remote_spec(const std::string &spec) {
    std::lock_guard<std::mutex> lock(mutex_);
    remote_spec_ = spec;
}

void TaskOrchestrator::set_download_hub(int hub) {
    std::lock_guard<std::mutex> lock(mutex_);
    download_hub_ = hub;
}

DownloadSpec TaskOrchestrator::make_download_spec_unlocked() const {
    DownloadSpec spec{};
    switch (download_hub_) {
        case 0:
            spec.hub = ModelHub::HuggingFace;
            break;
        case 1:
            spec.hub = ModelHub::ModelScope;
            break;
        default:
            spec.hub = ModelHub::Auto;
            break;
    }

    if (!download_parse_spec(remote_spec_, spec)) {
        throw std::runtime_error("invalid remote spec: " + remote_spec_);
    }
    return spec;
}

void TaskOrchestrator::emit_status(const std::string &message, bool busy) const {
    if (callbacks_.on_status) {
        callbacks_.on_status(message, busy);
    }
}

TaskId TaskOrchestrator::submit_download_model(TaskPriority priority) {
    DownloadModelPayload payload;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        payload.local_path = model_path_;
        payload.remote_spec = make_download_spec_unlocked();
    }

    Task task{};
    task.kind = TaskKind::DownloadModel;
    task.priority = priority;
    task.payload = std::move(payload);

    return queue_.enqueue(std::move(task));
}

TaskId TaskOrchestrator::submit_load_model(TaskPriority priority) {
    std::string model_path;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        model_path = model_path_;
    }

    Task task{};
    task.kind = TaskKind::LoadModel;
    task.priority = priority;
    task.payload = make_load_payload(std::move(model_path));

    return queue_.enqueue(std::move(task));
}

TaskId TaskOrchestrator::submit_unload_model(TaskPriority priority) {
    Task task{};
    task.kind = TaskKind::UnloadModel;
    task.priority = priority;
    task.payload = LoadModelPayload{};

    return queue_.enqueue(std::move(task));
}

TaskId TaskOrchestrator::submit_translate_pipeline(
    const TranslatePipelinePayload &payload,
    TaskPriority priority) {
    Task task{};
    task.kind = TaskKind::TranslatePipeline;
    task.priority = priority;
    task.payload = payload;

    apply_interactive_preemption(task);
    return queue_.enqueue(std::move(task));
}

void TaskOrchestrator::apply_interactive_preemption(const Task &incoming_task) {
    if (incoming_task.priority != TaskPriority::Interactive || incoming_task.kind != TaskKind::TranslatePipeline) {
        return;
    }

    const std::vector<TaskId> cancelled_pending = queue_.cancel_all_pending_normal_translate_tasks();
    for (const TaskId pending_id : cancelled_pending) {
        if (callbacks_.on_task_state_changed) {
            callbacks_.on_task_state_changed(pending_id.value, TaskState::Cancelled);
        }
        if (callbacks_.on_translation_finished) {
            callbacks_.on_translation_finished(pending_id.value, TaskState::Cancelled);
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (processing_ && running_task_id_.is_valid() && running_priority_ == TaskPriority::Normal && running_cancel_token_ != nullptr) {
        running_cancel_token_->cancel();
    }
}

bool TaskOrchestrator::cancel(TaskId id) {
    if (!id.is_valid()) {
        return false;
    }

    const bool affects_running = queue_.cancel(id);

    TaskId running_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (processing_ && running_task_id_ == id && running_cancel_token_ != nullptr) {
            running_cancel_token_->cancel();
            running_id = running_task_id_;
        }
    }

    if (affects_running || running_id.is_valid()) {
        return true;
    }

    if (queue_.state_of(id) == TaskState::Cancelled) {
        if (callbacks_.on_task_state_changed) {
            callbacks_.on_task_state_changed(id.value, TaskState::Cancelled);
        }
        if (callbacks_.on_translation_finished) {
            callbacks_.on_translation_finished(id.value, TaskState::Cancelled);
        }
        return true;
    }

    return false;
}

bool TaskOrchestrator::cancel_running() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (processing_ && running_cancel_token_ != nullptr) {
        running_cancel_token_->cancel();
        return true;
    }
    return false;
}

TaskState TaskOrchestrator::task_state(TaskId id) const {
    return queue_.state_of(id);
}

bool TaskOrchestrator::is_model_loaded() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return model_loaded_ && engine_.is_loaded();
}

void TaskOrchestrator::finalize_task(TaskId id, TaskKind kind, TaskState state) {
    queue_.set_state(id, state);
    if (callbacks_.on_task_state_changed) {
        callbacks_.on_task_state_changed(id.value, state);
    }
    if (kind == TaskKind::TranslatePipeline && callbacks_.on_translation_finished) {
        callbacks_.on_translation_finished(id.value, state);
    }
}

void TaskOrchestrator::process_next() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (processing_) {
            return;
        }
        processing_ = true;
    }

    while (true) {
        std::optional<Task> task = queue_.pop_next();
        if (!task.has_value()) {
            break;
        }

        execute_task(std::move(*task));
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        processing_ = false;
        running_task_id_ = TaskId{};
        running_cancel_token_.reset();
    }

    emit_status("Ready", false);
}

void TaskOrchestrator::execute_task(Task task) {
    const TaskId task_id = task.id;
    auto cancel_token = std::make_shared<CancelToken>();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_task_id_ = task_id;
        running_priority_ = task.priority;
        running_cancel_token_ = cancel_token;
    }

    if (callbacks_.on_task_state_changed) {
        callbacks_.on_task_state_changed(task_id.value, TaskState::Running);
    }

    try {
        switch (task.kind) {
            case TaskKind::DownloadModel: {
                emit_status("Downloading model", true);
                const auto &payload = std::get<DownloadModelPayload>(task.payload);

                download_set_quiet(true);
                download_set_progress_callback([this](const DownloadProgress &progress) {
                    if (callbacks_.on_download_progress) {
                        callbacks_.on_download_progress(
                            progress.downloaded_bytes,
                            progress.total_bytes,
                            progress.speed_bytes_per_sec,
                            progress.eta_seconds);
                    }
                });

                download_to_file(payload.local_path, payload.remote_spec, true);
                download_set_progress_callback(nullptr);

                emit_status("Download complete", false);
                if (callbacks_.on_download_finished) {
                    callbacks_.on_download_finished(true);
                }
                finalize_task(task_id, task.kind, TaskState::Completed);
                break;
            }
            case TaskKind::LoadModel: {
                emit_status("Loading model into memory", true);
                const auto &payload = std::get<LoadModelPayload>(task.payload);

                TranslationModelConfig config{};
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    config = model_config_;
                }

                engine_.load(payload.model_path, config);

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    model_loaded_ = true;
                }

                emit_status("Model loaded", false);
                if (callbacks_.on_model_load_finished) {
                    callbacks_.on_model_load_finished(true, "");
                }
                finalize_task(task_id, task.kind, TaskState::Completed);
                break;
            }
            case TaskKind::UnloadModel: {
                emit_status("Unloading model", true);
                engine_.unload();

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    model_loaded_ = false;
                }

                emit_status("Model unloaded", false);
                if (callbacks_.on_model_unload_finished) {
                    callbacks_.on_model_unload_finished();
                }
                finalize_task(task_id, task.kind, TaskState::Completed);
                break;
            }
            case TaskKind::TranslatePipeline: {
                const auto &payload = std::get<TranslatePipelinePayload>(task.payload);
                fprintf(stderr, "[Orchestrator] TranslatePipeline task:%llu src:'%s' target:'%s' back:%d\n",
                        static_cast<unsigned long long>(task_id.value),
                        payload.source.c_str(),
                        payload.target_language.c_str(),
                        payload.back_translate ? 1 : 0);
                emit_status("Translating", true);

                if (payload.source.empty()) {
                    throw std::runtime_error("enter text to translate");
                }

                if (!engine_.is_loaded()) {
                    throw std::runtime_error("model is not loaded");
                }

                const TranslateStepResult result = engine_.run_translate_pipeline(
                    payload,
                    [this, task_id](bool is_back_channel) {
                        if (is_back_channel) {
                            if (callbacks_.on_back_translate_reset) {
                                callbacks_.on_back_translate_reset(task_id.value);
                            }
                        } else if (callbacks_.on_target_reset) {
                            callbacks_.on_target_reset(task_id.value);
                        }
                    },
                    [this, task_id](bool is_back_channel, const std::string &piece) {
                        fprintf(stderr, "[Orchestrator] token:%s chan:%d piece:'%s'\n",
                                is_back_channel ? "back" : "fwd",
                                is_back_channel ? 1 : 0,
                                piece.c_str());
                        if (is_back_channel) {
                            if (callbacks_.on_back_translate_appended) {
                                callbacks_.on_back_translate_appended(task_id.value, piece);
                            }
                        } else if (callbacks_.on_target_appended) {
                            callbacks_.on_target_appended(task_id.value, piece);
                        }
                    },
                    cancel_token.get());

                if (result.outcome == InferenceOutcome::Cancelled) {
                    fprintf(stderr, "[Orchestrator] TranslatePipeline cancelled task:%llu\n",
                            static_cast<unsigned long long>(task_id.value));
                    emit_status("Cancelled", false);
                    finalize_task(task_id, task.kind, TaskState::Cancelled);
                } else if (result.outcome == InferenceOutcome::Failed) {
                    fprintf(stderr, "[Orchestrator] TranslatePipeline failed task:%llu err:%s\n",
                            static_cast<unsigned long long>(task_id.value), result.error_message.c_str());
                    emit_status(std::string("Error: ") + result.error_message, false);
                    finalize_task(task_id, task.kind, TaskState::Failed);
                } else {
                    fprintf(stderr, "[Orchestrator] TranslatePipeline completed task:%llu result_len:%zu\n",
                            static_cast<unsigned long long>(task_id.value), result.text.size());
                    emit_status("Done", false);
                    finalize_task(task_id, task.kind, TaskState::Completed);
                }
                break;
            }
        }
    } catch (const std::exception &ex) {
        if (task.kind == TaskKind::DownloadModel) {
            download_set_progress_callback(nullptr);
            emit_status(std::string("Error: ") + ex.what(), false);
            if (callbacks_.on_download_finished) {
                callbacks_.on_download_finished(false);
            }
        } else if (task.kind == TaskKind::LoadModel) {
            engine_.unload();
            {
                std::lock_guard<std::mutex> lock(mutex_);
                model_loaded_ = false;
            }
            emit_status(std::string("Error: ") + ex.what(), false);
            if (callbacks_.on_model_load_finished) {
                callbacks_.on_model_load_finished(false, ex.what());
            }
        } else {
            emit_status(std::string("Error: ") + ex.what(), false);
            if (task.kind == TaskKind::TranslatePipeline) {
                finalize_task(task_id, task.kind, TaskState::Failed);
            }
        }
    }
}
