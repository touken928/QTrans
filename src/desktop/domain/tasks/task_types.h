#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>

struct TaskId {
    std::uint64_t value = 0;

    bool operator==(const TaskId &other) const {
        return value == other.value;
    }
    bool operator!=(const TaskId &other) const {
        return value != other.value;
    }
    bool is_valid() const {
        return value != 0;
    }
};

enum class TaskKind {
    DownloadModel,
    LoadModel,
    UnloadModel,
    TranslatePipeline,
};

enum class TaskPriority {
    Interactive = 0,
    Normal = 1,
    Background = 2,
};

enum class TaskState {
    Pending,
    Running,
    Completed,
    Failed,
    Cancelled,
    Preempted,  // batch segment preempted by interactive work or pause
};

struct DownloadModelPayload {
    std::string local_path;
    std::string remote_spec;
    std::string modelscope_remote_spec;
    int download_hub = 2;
};

struct LoadModelPayload {
    std::string model_id;
    std::filesystem::path model_path;
};

struct TranslatePipelinePayload {
    std::string source;
    std::string target_language;
    std::string source_language;
    bool back_translate = false;
    bool wordselect = false;
};

using TaskPayload = std::variant<
    DownloadModelPayload,
    LoadModelPayload,
    TranslatePipelinePayload>;

struct Task {
    TaskId id;
    TaskKind kind = TaskKind::TranslatePipeline;
    TaskPriority priority = TaskPriority::Normal;
    TaskState state = TaskState::Pending;
    TaskPayload payload;
};

enum class InferenceOutcome {
    Completed,
    Cancelled,
    Failed,
};

struct TranslateStepResult {
    InferenceOutcome outcome = InferenceOutcome::Failed;
    std::string text;
    std::string error_message;
};

inline bool is_translate_kind(TaskKind kind) {
    return kind == TaskKind::TranslatePipeline;
}

inline bool is_system_kind(TaskKind kind) {
    return kind == TaskKind::DownloadModel || kind == TaskKind::LoadModel || kind == TaskKind::UnloadModel;
}
