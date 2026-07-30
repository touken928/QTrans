#pragma once

#include "qtrans/core.h"

namespace qtrans::core::diagnostics {

struct Callbacks {
    DiagnosticSink diagnostic_sink;
    TraceSink trace_sink;
};

void configure(Callbacks callbacks);
void emit(DiagnosticLevel level, std::string_view component, std::string_view message);
void emit_ai_trace(std::string_view prompt, std::string_view response);

}  // namespace qtrans::core::diagnostics
