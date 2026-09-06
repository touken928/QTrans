#include "diagnostics.h"

#include <mutex>
#include <utility>

namespace qtrans::core::diagnostics {

namespace {

Callbacks &callbacks_storage() {
    static Callbacks callbacks;
    return callbacks;
}

std::mutex &callbacks_mutex() {
    static std::mutex mutex;
    return mutex;
}

}  // namespace

void configure(Callbacks callbacks) {
    std::lock_guard<std::mutex> lock(callbacks_mutex());
    callbacks_storage() = std::move(callbacks);
}

void emit(DiagnosticLevel level, std::string_view component, std::string_view message) {
    DiagnosticSink sink;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex());
        sink = callbacks_storage().diagnostic_sink;
    }
    if (sink) {
        try {
            sink(level, component, message);
        } catch (...) {
        }
    }
}

void emit_ai_trace(std::string_view prompt, std::string_view response) {
    TraceSink sink;
    {
        std::lock_guard<std::mutex> lock(callbacks_mutex());
        sink = callbacks_storage().trace_sink;
    }
    if (sink) {
        try {
            sink(prompt, response);
        } catch (...) {
        }
    }
}

}  // namespace qtrans::core::diagnostics
