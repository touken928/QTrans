#include "domain/inference/production_translation_session.h"

#include "domain/inference/inference_resolver.h"
#include "domain/inference/runtime_capabilities.h"
#include "domain/model-adapters/local_model_factory.h"
#include "domain/model-catalog/model_catalog.h"
#include "domain/logging/ai_trace.h"
#include "domain/logging/component.h"
#include "domain/logging/logger.h"
#include "qtrans/core.h"

#include <sstream>
#include <stdexcept>

namespace {
std::string backend_kind_label(qtrans::core::Backend backend) {
    switch (backend) {
        case qtrans::core::Backend::Vulkan:
            return "Vulkan";
        case qtrans::core::Backend::Metal:
            return "Metal";
        default:
            return "CPU";
    }
}

std::string enrich_error(const std::string &message,
                         const qtrans::core::BackendState &environment,
                         qtrans::core::Backend requested) {
    std::ostringstream result;
    result << message << "\nBackend: requested " << backend_kind_label(requested)
           << ", resolved " << environment.label;
    bool first = true;
    for (const auto &diagnostic : environment.capabilities.diagnostics) {
        if (diagnostic.backend == requested) {
            result << (first ? "\nDetails: " : "; ") << diagnostic.message;
            first = false;
        }
    }
    return result.str();
}
}  // namespace

void ProductionTranslationSession::initialize_backend() {
    qtrans::core::BackendInitializationOptions options;
    options.diagnostic_sink = [](qtrans::core::DiagnosticLevel level,
                                 std::string_view component,
                                 std::string_view message) {
        auto logger = qtrans::log::get(component == "llama" ? qtrans::log::Component::Hymt
                                                            : qtrans::log::Component::Inference);
        if (!logger) return;
        switch (level) {
            case qtrans::core::DiagnosticLevel::Error:
                logger->error("{}", message);
                break;
            case qtrans::core::DiagnosticLevel::Warn:
                logger->warn("{}", message);
                break;
            case qtrans::core::DiagnosticLevel::Info:
                logger->info("{}", message);
                break;
            case qtrans::core::DiagnosticLevel::Debug:
                logger->debug("{}", message);
                break;
            case qtrans::core::DiagnosticLevel::Trace:
                logger->trace("{}", message);
                break;
        }
    };
    options.trace_sink = [](std::string_view prompt, std::string_view response) {
        qtrans::log::write_ai_trace(std::string(prompt), std::string(response));
    };
    qtrans::core::configure_backend(options);
    RuntimeCapabilities::instance().refresh(qtrans::core::initialize_backend());
}

std::string ProductionTranslationSession::active_backend_label() const {
    return engine_.active_backend_label();
}
bool ProductionTranslationSession::is_loaded() const {
    return engine_.is_loaded();
}

ExecutionResult ProductionTranslationSession::load(const LoadModelPayload &payload) {
    try {
        const ModelCatalogEntry *entry = find_model_by_id(payload.model_id);
        if (entry == nullptr) throw std::runtime_error("unknown model id: " + payload.model_id);
        const RuntimeCapabilities &caps = RuntimeCapabilities::instance();
        const auto resolved = resolve_inference(*entry, caps);
        if (!resolved) throw std::runtime_error(unavailable_reason(*entry, caps));
        engine_.set_backend(resolved->backend);
        try {
            engine_.load(create_local_model(*entry, payload.model_path));
        } catch (const std::exception &ex) {
            throw std::runtime_error(enrich_error(ex.what(), caps.environment(), resolved->backend));
        }
        return {ExecutionOutcome::Completed, {}};
    } catch (const std::exception &ex) {
        return {ExecutionOutcome::Failed, ex.what()};
    }
}

ExecutionResult ProductionTranslationSession::unload() {
    try {
        engine_.unload();
        return {ExecutionOutcome::Completed, {}};
    } catch (const std::exception &ex) {
        return {ExecutionOutcome::Failed, ex.what()};
    }
}

ExecutionResult ProductionTranslationSession::translate(const TranslatePipelinePayload &payload,
                                                        TranslationResetHandler on_reset,
                                                        TranslationTokenHandler on_token,
                                                        const CancelToken *cancel_token) {
    if (payload.source.empty()) return {ExecutionOutcome::Failed, "enter text to translate"};
    if (!engine_.is_loaded()) return {ExecutionOutcome::Failed, "model is not loaded"};
    const TranslateStepResult result = engine_.run_translate_pipeline(payload, std::move(on_reset), std::move(on_token), cancel_token);
    if (result.outcome == InferenceOutcome::Completed) return {ExecutionOutcome::Completed, {}};
    if (result.outcome == InferenceOutcome::Cancelled) return {ExecutionOutcome::Cancelled, {}};
    return {ExecutionOutcome::Failed, result.error_message};
}
