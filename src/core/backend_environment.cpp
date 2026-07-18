#include "qtrans/core/backend_environment.h"

#include "diagnostics.h"

#include "ggml-backend.h"
#include "llama.h"

#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace qtrans::core {

namespace {

struct BackendEnvironmentState {
    ResolvedBackendEnvironment resolved;
};

BackendEnvironmentState &state() {
    static BackendEnvironmentState backend_state;
    return backend_state;
}

std::string backend_kind_label(BackendKind backend) {
    switch (backend) {
        case BackendKind::Metal:
            return "Metal";
        case BackendKind::Vulkan:
            return "Vulkan";
        case BackendKind::Cpu:
        default:
            return "CPU";
    }
}

void emit_diagnostic(DiagnosticLevel level, std::string_view component, std::string_view message) {
    diagnostics::emit(level, component, message);
}

void add_backend_diagnostic(BackendCapabilities &caps,
                            BackendKind backend,
                            std::string code,
                            std::string message,
                            bool user_actionable,
                            DiagnosticLevel level) {
    caps.diagnostics.push_back(BackendDiagnostic{backend, std::move(code), std::move(message), user_actionable});
    emit_diagnostic(level, "backend", caps.diagnostics.back().message);
}

bool probe_vulkan_gpu(BackendCapabilities &caps) {
#ifdef QTRANS_MULTI_BACKEND
    const ggml_backend_reg_t reg = ggml_backend_reg_by_name("Vulkan");
    if (reg == nullptr) {
        add_backend_diagnostic(caps, BackendKind::Vulkan, "backend_not_registered",
                               "Vulkan backend did not register with ggml", true,
                               DiagnosticLevel::Warn);
        return false;
    }
    const size_t device_count = ggml_backend_reg_dev_count(reg);
    if (device_count == 0) {
        add_backend_diagnostic(caps, BackendKind::Vulkan, "no_devices",
                               "Vulkan backend registered but reported no GPU devices", true,
                               DiagnosticLevel::Warn);
    }
    for (size_t i = 0; i < device_count; ++i) {
        const ggml_backend_dev_t device = ggml_backend_reg_dev_get(reg, i);
        if (ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_GPU) return true;
    }
    const ggml_backend_dev_t gpu = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU);
    if (gpu == nullptr) {
        add_backend_diagnostic(caps, BackendKind::Vulkan, "gpu_lookup_failed",
                               "No Vulkan GPU device was selected from ggml backend registry",
                               true, DiagnosticLevel::Warn);
    }
    return gpu != nullptr;
#else
    add_backend_diagnostic(caps, BackendKind::Vulkan, "backend_not_compiled",
                           "Vulkan backend support is not compiled into this build", false,
                           DiagnosticLevel::Info);
    return false;
#endif
}

bool probe_metal_gpu(BackendCapabilities &caps) {
#ifdef QTRANS_GPU_METAL
    const ggml_backend_reg_t reg = ggml_backend_reg_by_name("MTL");
    if (reg == nullptr) {
        add_backend_diagnostic(caps, BackendKind::Metal, "backend_not_registered",
                               "Metal backend did not register with ggml", true,
                               DiagnosticLevel::Warn);
        return false;
    }
    const size_t device_count = ggml_backend_reg_dev_count(reg);
    if (device_count == 0) {
        add_backend_diagnostic(caps, BackendKind::Metal, "no_devices",
                               "Metal backend registered but reported no GPU devices", true,
                               DiagnosticLevel::Warn);
    }
    for (size_t i = 0; i < device_count; ++i) {
        const ggml_backend_dev_t device = ggml_backend_reg_dev_get(reg, i);
        if (ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_GPU) return true;
    }
    const ggml_backend_dev_t gpu = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU);
    if (gpu == nullptr) {
        add_backend_diagnostic(caps, BackendKind::Metal, "gpu_lookup_failed",
                               "No Metal GPU device was selected from ggml backend registry",
                               true, DiagnosticLevel::Warn);
    }
    return gpu != nullptr;
#else
    add_backend_diagnostic(caps, BackendKind::Metal, "backend_not_compiled",
                           "Metal backend support is not compiled into this build", false,
                           DiagnosticLevel::Info);
    return false;
#endif
}

BackendKind resolve_selected_backend(const BackendOptions &options,
                                     BackendCapabilities &caps) {
    const bool vulkan_available = caps.vulkan_available;
    const bool metal_available = caps.metal_available;
    switch (options.backend_type) {
        case BackendType::Vulkan:
            if (vulkan_available) return BackendKind::Vulkan;
            add_backend_diagnostic(caps, BackendKind::Vulkan, "requested_backend_unavailable",
                                   "Vulkan backend was explicitly requested but is unavailable",
                                   true, DiagnosticLevel::Error);
            return BackendKind::Cpu;
        case BackendType::Metal:
            if (metal_available) return BackendKind::Metal;
            add_backend_diagnostic(caps, BackendKind::Metal, "requested_backend_unavailable",
                                   "Metal backend was explicitly requested but is unavailable",
                                   true, DiagnosticLevel::Error);
            return BackendKind::Cpu;
        case BackendType::Auto:
        default:
            if (vulkan_available) return BackendKind::Vulkan;
            if (metal_available) return BackendKind::Metal;
            return BackendKind::Cpu;
    }
}

void log_llama_text(ggml_log_level level, const char *text) {
    if (text == nullptr) return;
    std::string_view message(text);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
        message.remove_suffix(1);
    }
    if (message.empty()) return;

    switch (level) {
        case GGML_LOG_LEVEL_ERROR:
            emit_diagnostic(DiagnosticLevel::Error, "llama", message);
            break;
#ifndef NDEBUG
        case GGML_LOG_LEVEL_WARN:
            emit_diagnostic(DiagnosticLevel::Warn, "llama", message);
            break;
        case GGML_LOG_LEVEL_INFO:
            emit_diagnostic(DiagnosticLevel::Info, "llama", message);
            break;
        case GGML_LOG_LEVEL_DEBUG:
            emit_diagnostic(DiagnosticLevel::Debug, "llama", message);
            break;
#endif
        default:
            break;
    }
}

void set_llama_log_callback() {
    llama_log_set([](ggml_log_level level, const char *text, void *) { log_llama_text(level, text); },
                  nullptr);
}

}  // namespace

const ResolvedBackendEnvironment &BackendEnvironment::initialize_and_resolve(
    const BackendInitializationOptions &options) {
    static std::once_flag once;
    std::call_once(once, [&options]() {
        diagnostics::configure({options.diagnostic_sink, options.ai_trace_sink});
        set_llama_log_callback();
        llama_backend_init();
#ifdef QTRANS_MULTI_BACKEND
        ggml_backend_load_all();
#endif
        auto &resolved = state().resolved;
        resolved.initialized = true;
        resolved.capabilities = BackendCapabilities{};
        resolved.capabilities.vulkan_available = probe_vulkan_gpu(resolved.capabilities);
        resolved.capabilities.metal_available = probe_metal_gpu(resolved.capabilities);
        resolved.selected = resolve_selected_backend(options.backend, resolved.capabilities);
        resolved.label = backend_kind_label(resolved.selected);
    });
    return state().resolved;
}

const ResolvedBackendEnvironment &BackendEnvironment::current() {
    return state().resolved;
}

}  // namespace qtrans::core
