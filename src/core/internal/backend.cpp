#include "qtrans/core.h"

#include "diagnostics.h"

#include "ggml-backend.h"
#include "llama.h"

#include <mutex>
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>

namespace qtrans::core {

namespace {

struct BackendEnvironmentState {
    BackendState resolved;
    bool initialized = false;
    bool locked = false;
};

BackendEnvironmentState &state() {
    static BackendEnvironmentState backend_state;
    return backend_state;
}

std::mutex &environment_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::string backend_kind_label(Backend backend) {
    switch (backend) {
        case Backend::Metal:
            return "Metal";
        case Backend::Vulkan:
            return "Vulkan";
        case Backend::Cpu:
        default:
            return "CPU";
    }
}

void emit_diagnostic(DiagnosticLevel level, std::string_view component, std::string_view message) {
    diagnostics::emit(level, component, message);
}

void add_backend_diagnostic(BackendCapabilities &caps,
                            Backend backend,
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
        add_backend_diagnostic(caps, Backend::Vulkan, "backend_not_registered",
                               "Vulkan backend did not register with ggml", true,
                               DiagnosticLevel::Warn);
        return false;
    }
    const size_t device_count = ggml_backend_reg_dev_count(reg);
    if (device_count == 0) {
        add_backend_diagnostic(caps, Backend::Vulkan, "no_devices",
                               "Vulkan backend registered but reported no GPU devices", true,
                               DiagnosticLevel::Warn);
    }
    for (size_t i = 0; i < device_count; ++i) {
        const ggml_backend_dev_t device = ggml_backend_reg_dev_get(reg, i);
        if (ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_GPU) return true;
    }
    const ggml_backend_dev_t gpu = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU);
    if (gpu == nullptr) {
        add_backend_diagnostic(caps, Backend::Vulkan, "gpu_lookup_failed",
                               "No Vulkan GPU device was selected from ggml backend registry",
                               true, DiagnosticLevel::Warn);
    }
    return gpu != nullptr;
#else
    add_backend_diagnostic(caps, Backend::Vulkan, "backend_not_compiled",
                           "Vulkan backend support is not compiled into this build", false,
                           DiagnosticLevel::Info);
    return false;
#endif
}

bool probe_metal_gpu(BackendCapabilities &caps) {
#ifdef QTRANS_GPU_METAL
    const ggml_backend_reg_t reg = ggml_backend_reg_by_name("MTL");
    if (reg == nullptr) {
        add_backend_diagnostic(caps, Backend::Metal, "backend_not_registered",
                               "Metal backend did not register with ggml", true,
                               DiagnosticLevel::Warn);
        return false;
    }
    const size_t device_count = ggml_backend_reg_dev_count(reg);
    if (device_count == 0) {
        add_backend_diagnostic(caps, Backend::Metal, "no_devices",
                               "Metal backend registered but reported no GPU devices", true,
                               DiagnosticLevel::Warn);
    }
    for (size_t i = 0; i < device_count; ++i) {
        const ggml_backend_dev_t device = ggml_backend_reg_dev_get(reg, i);
        if (ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_GPU) return true;
    }
    const ggml_backend_dev_t gpu = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU);
    if (gpu == nullptr) {
        add_backend_diagnostic(caps, Backend::Metal, "gpu_lookup_failed",
                               "No Metal GPU device was selected from ggml backend registry",
                               true, DiagnosticLevel::Warn);
    }
    return gpu != nullptr;
#else
    add_backend_diagnostic(caps, Backend::Metal, "backend_not_compiled",
                           "Metal backend support is not compiled into this build", false,
                           DiagnosticLevel::Info);
    return false;
#endif
}

Backend resolve_selected_backend(Backend requested, BackendCapabilities &caps) {
    const bool vulkan_available = caps.vulkan_available;
    const bool metal_available = caps.metal_available;
    switch (requested) {
        case Backend::Cpu:
            return Backend::Cpu;
        case Backend::Vulkan:
            if (vulkan_available) return Backend::Vulkan;
            add_backend_diagnostic(caps, Backend::Vulkan, "requested_backend_unavailable",
                                   "Vulkan backend was explicitly requested but is unavailable",
                                   true, DiagnosticLevel::Error);
            return Backend::Cpu;
        case Backend::Metal:
            if (metal_available) return Backend::Metal;
            add_backend_diagnostic(caps, Backend::Metal, "requested_backend_unavailable",
                                   "Metal backend was explicitly requested but is unavailable",
                                   true, DiagnosticLevel::Error);
            return Backend::Cpu;
        case Backend::Automatic:
        default:
            if (vulkan_available) return Backend::Vulkan;
            if (metal_available) return Backend::Metal;
            return Backend::Cpu;
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

void configure_backend(const BackendInitializationOptions &options) {
    diagnostics::configure({options.diagnostic_sink, options.trace_sink});
    set_llama_log_callback();
}

BackendState initialize_backend(Backend requested) {
    std::lock_guard<std::mutex> lock(environment_mutex());
    auto &backend_state = state();
    if (!backend_state.initialized) {
        llama_backend_init();
#ifdef QTRANS_MULTI_BACKEND
        ggml_backend_load_all();
#endif
        auto &resolved = backend_state.resolved;
        resolved.initialized = true;
        resolved.capabilities = BackendCapabilities{};
        resolved.capabilities.vulkan_available = probe_vulkan_gpu(resolved.capabilities);
        resolved.capabilities.metal_available = probe_metal_gpu(resolved.capabilities);
        backend_state.initialized = true;
    }

    const bool explicit_request = requested != Backend::Automatic;
    if (explicit_request) {
        const bool available = requested == Backend::Cpu ||
                               (requested == Backend::Metal &&
                                backend_state.resolved.capabilities.metal_available) ||
                               (requested == Backend::Vulkan &&
                                backend_state.resolved.capabilities.vulkan_available);
        if (!available) throw std::runtime_error("requested backend is unavailable");
    }

    if (backend_state.locked && !explicit_request) return backend_state.resolved;
    const Backend selected = resolve_selected_backend(requested,
                                                      backend_state.resolved.capabilities);
    if (backend_state.locked && selected != backend_state.resolved.selected) {
        throw std::runtime_error("requested backend conflicts with the initialized backend");
    }
    if (!backend_state.locked) {
        backend_state.resolved.selected = selected;
        backend_state.resolved.label = backend_kind_label(selected);
        backend_state.locked = true;
    }
    return backend_state.resolved;
}

BackendState backend_state() {
    std::lock_guard<std::mutex> lock(environment_mutex());
    return state().resolved;
}

}  // namespace qtrans::core
