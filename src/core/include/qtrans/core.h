#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
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

using PromptFormatter = std::function<std::string(std::string_view, std::string_view)>;

struct GenerationOptions {
    int context_tokens = 4096;
    int max_output_tokens = 4096;
    float temperature = 0.7f;
    int top_k = 20;
    float top_p = 0.6f;
    float repeat_penalty = 1.05f;
};

struct Model {
    std::filesystem::path path;
    PromptFormatter prompt_formatter;
    GenerationOptions generation;
};

enum class OverflowPolicy {
    Split,
    Reject,
};

struct TranslationRequest {
    std::string text;
    std::string target_language;
    OverflowPolicy overflow = OverflowPolicy::Split;
};

enum class TranslationOutcome {
    Completed,
    Cancelled,
    Failed,
};

enum class TranslationErrorCode {
    None,
    NotLoaded,
    InvalidRequest,
    ContextLimit,
    Cancelled,
    Runtime,
    Callback,
};

struct TranslationResult {
    TranslationOutcome outcome = TranslationOutcome::Failed;
    TranslationErrorCode error_code = TranslationErrorCode::Runtime;
    std::string text;
    std::string message;
};

using TokenSink = std::function<void(std::string_view)>;
using StopPredicate = std::function<bool()>;

class Translator {
public:
    explicit Translator(Backend backend = Backend::Automatic);
    ~Translator();

    Translator(const Translator &) = delete;
    Translator &operator=(const Translator &) = delete;
    Translator(Translator &&) noexcept;
    Translator &operator=(Translator &&) noexcept;

    void load(Model model);
    void unload() noexcept;
    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] Backend backend() const noexcept;

    // PromptFormatter and TokenSink string_views are valid only during the call.
    // A Translator instance is not thread-safe and does not support re-entry.
    TranslationResult translate(const TranslationRequest &request,
                                TokenSink token_sink = {},
                                StopPredicate stop = {});

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace qtrans::core
