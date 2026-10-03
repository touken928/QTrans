#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <cstddef>

// Typed desktop inference domain contracts. These types are Qt-free and live
// outside core; InferenceService adapts them to and from the core ModelHost
// invocation domain.

struct TranslationJobId {
    std::uint64_t value = 0;

    bool operator==(const TranslationJobId &other) const {
        return value == other.value;
    }
    bool operator!=(const TranslationJobId &other) const {
        return value != other.value;
    }
    bool is_valid() const {
        return value != 0;
    }
};

enum class TranslationState {
    Pending,
    Running,
    Completed,
    Failed,
    Cancelled,
    Preempted,
};

enum class TranslationChannel {
    Target,
    BackTranslate,
};

struct NativeTranslationRequest {
    std::string source;
    std::string target_language;
    std::string source_language;
    bool back_translate = false;
    bool wordselect = false;
};

struct BatchTranslationRequest {
    std::string source;
    std::string target_language;
    std::string source_language;
};

struct TranslationJobResult {
    TranslationJobId id;
    TranslationState state = TranslationState::Pending;
    std::string error_message;
};

class TranslationCancellation {
public:
    bool request();
    bool requested() const;
    void install(std::function<void()> callback);
    void complete();

private:
    mutable std::mutex mutex_;
    bool requested_ = false;
    bool completed_ = false;
    std::function<void()> callback_;
};

// Stable submission capability. Cancellation is sticky even before the core
// invocation handle is installed, eliminating the submit/cancel race at the
// API boundary. The legacy ID methods remain as compatibility adapters.
struct TranslationJobTicket {
    TranslationJobId id;
    std::shared_ptr<TranslationCancellation> cancellation;

    bool cancel() const;
};

enum class RuntimeLifecycleState {
    Unloaded,
    Loading,
    Ready,
    Unloading,
    Draining,
    ShuttingDown,
    Stopped,
};

// Single immutable projection of model lifecycle and admitted work. UI and
// API glue can consume this instead of combining several independently timed
// booleans and status strings.
struct RuntimeSnapshot {
    RuntimeLifecycleState lifecycle = RuntimeLifecycleState::Unloaded;
    std::string loaded_model_id;
    std::string backend_label;
    std::size_t active_translation_jobs = 0;
    std::size_t active_api_jobs = 0;
    bool supports_conversation = false;
};
