#pragma once

#include "qtrans/core/backend_environment.h"

#include <string_view>

namespace qtrans::core::diagnostics {

struct Callbacks {
    DiagnosticSink diagnostic_sink;
    AiTraceSink ai_trace_sink;
};

void configure(Callbacks callbacks);
void emit(DiagnosticLevel level, std::string_view component, std::string_view message);
void emit_ai_trace(std::string_view prompt, std::string_view response);

}  // namespace qtrans::core::diagnostics
