#pragma once

#include "qtrans/core/options.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace qtrans::core {

enum class BackendKind {
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
    std::function<void(DiagnosticLevel level, std::string_view component, std::string_view message)>;
using AiTraceSink =
    std::function<void(std::string_view prompt, std::string_view response)>;

struct BackendDiagnostic {
    BackendKind backend = BackendKind::Cpu;
    std::string code;
    std::string message;
    bool user_actionable = false;
};

struct BackendCapabilities {
    bool metal_available = false;
    bool vulkan_available = false;
    std::vector<BackendDiagnostic> diagnostics;
};

struct ResolvedBackendEnvironment {
    bool initialized = false;
    BackendKind selected = BackendKind::Cpu;
    BackendCapabilities capabilities;
    std::string label = "CPU";
};

struct BackendInitializationOptions {
    BackendOptions backend;
    DiagnosticSink diagnostic_sink;
    AiTraceSink ai_trace_sink;
};

class BackendEnvironment {
public:
    static const ResolvedBackendEnvironment &initialize_and_resolve(
        const BackendInitializationOptions &options = {});
    static const ResolvedBackendEnvironment &current();
};

}  // namespace qtrans::core
