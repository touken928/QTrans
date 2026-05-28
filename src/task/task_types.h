#pragma once

#include "network/download.h"

#include <cstdint>
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
};

enum class TaskState {
    Pending,
    Running,
    Completed,
    Failed,
    Cancelled,
};

struct DownloadModelPayload {
    std::string local_path;
    DownloadSpec remote_spec;
};

struct LoadModelPayload {
    std::string model_path;
};

struct TranslatePipelinePayload {
    std::string source;
    std::string target_language;
    std::string source_language;
    bool back_translate = false;
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
