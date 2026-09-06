#pragma once

#include <cstdint>
#include <cstddef>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace qtrans::core {

enum class Backend {
    Automatic,
    Cpu,
    Metal,
    Vulkan,
};

enum class DiagnosticLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
};

using DiagnosticSink =
    std::function<void(DiagnosticLevel, std::string_view, std::string_view)>;
using TraceSink = std::function<void(std::string_view, std::string_view)>;

struct BackendDiagnostic {
    Backend backend = Backend::Cpu;
    std::string code;
    std::string message;
    bool user_actionable = false;
};

struct BackendCapabilities {
    bool metal_available = false;
    bool vulkan_available = false;
    std::vector<BackendDiagnostic> diagnostics;
};

struct BackendState {
    bool initialized = false;
    Backend selected = Backend::Cpu;
    BackendCapabilities capabilities;
    std::string label = "CPU";
};

struct BackendInitializationOptions {
    DiagnosticSink diagnostic_sink;
    TraceSink trace_sink;
};

void configure_backend(const BackendInitializationOptions &options);
BackendState initialize_backend(Backend backend = Backend::Automatic);
BackendState backend_state();

enum class OverflowPolicy {
    Split,
    Reject,
};

struct InvocationId {
    std::uint64_t value = 0;
    friend bool operator==(InvocationId left, InvocationId right) {
        return left.value == right.value;
    }
    friend bool operator!=(InvocationId left, InvocationId right) {
        return !(left == right);
    }
};

struct ModelId {
    std::string value;
    friend bool operator==(const ModelId &left, const ModelId &right) {
        return left.value == right.value;
    }
    friend bool operator!=(const ModelId &left, const ModelId &right) {
        return !(left == right);
    }
};

struct LanguageTag {
    std::string value;
    friend bool operator==(const LanguageTag &left, const LanguageTag &right) {
        return left.value == right.value;
    }
    friend bool operator!=(const LanguageTag &left, const LanguageTag &right) {
        return !(left == right);
    }
};

enum class Role { System,
                  User,
                  Assistant };

struct Message {
    Role role = Role::User;
    std::string content;
};

struct ConversationInput {
    std::vector<Message> messages;
};

struct TranslationInput {
    std::string text;
    std::optional<LanguageTag> source_language;
    LanguageTag target_language;
    OverflowPolicy overflow = OverflowPolicy::Split;
};

using InvocationInput = std::variant<ConversationInput, TranslationInput>;

struct SamplingOptions {
    std::uint32_t max_output_tokens = 0;
    float temperature = 0.7f;
    float top_p = 0.95f;
    std::uint32_t seed = 0;
};

enum class WorkClass { NativeInteractive,
                       ApiInteractive,
                       NativeNormal,
                       Batch };

enum class StopReason { None,
                        Preempted,
                        Deadline,
                        UserCancel,
                        Shutdown };

enum class FailureCode {
    None,
    InvalidRequest,
    ContextLimit,
    UnsupportedModel,
    UnsupportedCapability,
    Backpressure,
    Deadline,
    NotLoaded,
    LifecycleTransition,
    AlreadyExists,
    Cancelled,
    Runtime,
    Shutdown,
    Observer,
};

struct Failure {
    FailureCode code = FailureCode::None;
    std::string message;
    explicit operator bool() const noexcept {
        return code != FailureCode::None;
    }
};

enum class FinishReason { Completed,
                          Stop,
                          Length,
                          Cancelled,
                          Preempted,
                          Deadline,
                          Failed };

struct TokenUsage {
    std::uint64_t input_tokens = 0;
    std::uint64_t output_tokens = 0;
    std::uint64_t total_tokens = 0;
};

struct Timing {
    std::uint64_t queue_milliseconds = 0;
    std::uint64_t generation_milliseconds = 0;
};

struct InvocationResult {
    InvocationId id;
    FinishReason finish_reason = FinishReason::Failed;
    StopReason stop_reason = StopReason::None;
    std::string output;
    TokenUsage usage;
    Timing timing;
    std::optional<Failure> failure;
};

struct InvocationStarted {
    InvocationId id;
    std::uint64_t sequence = 0;
};

struct InvocationDelta {
    InvocationId id;
    std::string text;
    std::uint64_t sequence = 0;
};

struct InvocationFinished {
    InvocationResult result;
    std::uint64_t sequence = 0;
};

using InvocationEvent = std::variant<InvocationStarted, InvocationDelta, InvocationFinished>;
using InvocationEventSink = std::function<void(const InvocationEvent &)>;

struct InvocationRequest {
    ModelId model;
    InvocationInput input;
    SamplingOptions sampling;
    WorkClass work_class = WorkClass::NativeInteractive;
    std::optional<std::chrono::steady_clock::time_point> deadline;
    std::string client_request_id;
};

enum class LifecycleState { Unloaded,
                            Loading,
                            Ready,
                            Unloading,
                            Draining,
                            ShuttingDown,
                            Stopped };

struct LifecycleSnapshot {
    LifecycleState state = LifecycleState::Unloaded;
    std::optional<ModelId> model;
    std::size_t active_invocations = 0;
    std::optional<Failure> failure;
    // True when the currently loaded model's prompt profile accepts
    // ConversationInput. Populated on successful load (Ready) and cleared by
    // the existing `LifecycleSnapshot{}` reset on unload/load failure.
    bool supports_conversation = false;
};

struct OperationResult {
    bool accepted = false;
    Failure failure;
    bool deferred = false;
    explicit operator bool() const noexcept {
        return accepted;
    }
};

class ModelHost;

class InvocationHandle {
public:
    InvocationHandle() = default;
    InvocationId id() const noexcept {
        return id_;
    }
    OperationResult cancel() const;
    explicit operator bool() const noexcept {
        return static_cast<bool>(cancel_function_);
    }

private:
    friend class ModelHost;
    InvocationHandle(InvocationId id, std::function<OperationResult()> cancel_function)
        : id_(id), cancel_function_(std::move(cancel_function)) {
    }
    InvocationId id_;
    std::function<OperationResult()> cancel_function_;
};

struct SubmitResult {
    bool accepted = false;
    InvocationId id;
    Failure failure;
    InvocationHandle handle;
    explicit operator bool() const noexcept {
        return accepted;
    }
};

struct ModelSpec {
    ModelId id;
    std::filesystem::path path;
};

class ModelHost {
public:
    struct Options {
        InvocationEventSink event_sink;
    };

    explicit ModelHost(Options options = {});
    ~ModelHost();

    ModelHost(const ModelHost &) = delete;
    ModelHost &operator=(const ModelHost &) = delete;

    OperationResult load(const ModelSpec &model);
    OperationResult unload();
    SubmitResult submit(const InvocationRequest &request, InvocationEventSink observer = {});
    OperationResult cancel(InvocationId id);
    OperationResult preempt(InvocationId id);
    LifecycleSnapshot snapshot() const;
    OperationResult shutdown();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

}  // namespace qtrans::core
