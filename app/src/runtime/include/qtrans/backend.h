#pragma once

#include <functional>
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

}  // namespace qtrans::core
