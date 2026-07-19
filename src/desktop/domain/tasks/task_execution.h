#pragma once

#include "domain/tasks/cancel_token.h"
#include "domain/tasks/task_types.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

enum class CancellationReason {
    None,
    User,
    Interactive,
    Pause,
};

inline bool is_preemption_reason(CancellationReason reason) {
    return reason == CancellationReason::Interactive || reason == CancellationReason::Pause;
}

// Preemption has priority over user cancellation. Once published, it cannot be downgraded.
inline CancellationReason merge_cancellation_reason(
    CancellationReason current,
    CancellationReason incoming) {
    if (current == CancellationReason::None) return incoming;
    if (incoming == CancellationReason::None) return current;
    if (is_preemption_reason(current)) return current;
    if (is_preemption_reason(incoming)) return incoming;
    return CancellationReason::User;
}

enum class ExecutionOutcome {
    Completed,
    Cancelled,
    Failed,
};

struct ExecutionResult {
    ExecutionOutcome outcome = ExecutionOutcome::Failed;
    std::string error_message;
};

struct DownloadProgressData {
    std::int64_t downloaded_bytes = 0;
    std::int64_t total_bytes = 0;
    double speed_bps = 0.0;
    double eta_seconds = -1.0;
};

using DownloadProgressHandler = std::function<void(const DownloadProgressData &)>;
using TranslationResetHandler = std::function<void(bool is_back_channel)>;
using TranslationTokenHandler = std::function<void(bool is_back_channel, const std::string &piece)>;

class IModelDownloader {
public:
    virtual ~IModelDownloader() = default;
    virtual ExecutionResult download(
        const DownloadModelPayload &payload,
        const CancelToken *cancel_token,
        DownloadProgressHandler on_progress) = 0;
};

class ITranslationSession {
public:
    virtual ~ITranslationSession() = default;
    virtual void initialize_backend() = 0;
    virtual std::string active_backend_label() const = 0;
    virtual bool is_loaded() const = 0;
    virtual ExecutionResult load(const LoadModelPayload &payload) = 0;
    virtual ExecutionResult unload() = 0;
    virtual ExecutionResult translate(
        const TranslatePipelinePayload &payload,
        TranslationResetHandler on_reset,
        TranslationTokenHandler on_token,
        const CancelToken *cancel_token) = 0;
};

struct TaskExecutors {
    std::unique_ptr<IModelDownloader> downloader;
    std::unique_ptr<ITranslationSession> translation_session;
};
