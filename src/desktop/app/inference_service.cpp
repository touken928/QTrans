#include "app/inference_service.h"

#include "domain/inference/runtime_capabilities.h"
#include "domain/logging/ai_trace.h"
#include "domain/logging/component.h"
#include "domain/logging/logger.h"
#include "shared/string_bridge.h"

#include <QMetaObject>
#include <QThread>

#include <filesystem>

namespace {

qtrans::core::LanguageTag language(const std::string &value) {
    return {value.empty() ? "Auto" : value};
}

}  // namespace

InferenceService::InferenceService(QObject *parent)
    : QObject(parent), host_({}) {
    qRegisterMetaType<TranslationJobId>("TranslationJobId");
    qRegisterMetaType<TranslationState>("TranslationState");
    qRegisterMetaType<TranslationChannel>("TranslationChannel");
    qRegisterMetaType<TranslationJobResult>("TranslationJobResult");
}

InferenceService::~InferenceService() {
    if (host_.snapshot().state != qtrans::core::LifecycleState::Stopped) host_.shutdown();
}

void InferenceService::setModelConfig(const QString &model_id, const QString &model_path) {
    std::lock_guard lock(mutex_);
    model_id_ = qtrans::app::to_utf8(model_id);
    model_path_ = qtrans::app::to_utf8(model_path);
}

TranslationJobId InferenceService::translateNative(const NativeTranslationRequest &request) {
    TranslationJobId id;
    {
        std::lock_guard lock(mutex_);
        id = TranslationJobId{next_job_id_++};
        JobRecord record;
        record.id = id;
        record.request = request;
        record.back_translate = request.back_translate;
        jobs_.emplace(id.value, std::move(record));
    }
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, id, request] { submitJob(id, request, TranslationChannel::Target, false); }, Qt::QueuedConnection);
    } else {
        submitJob(id, request, TranslationChannel::Target, false);
    }
    return id;
}

TranslationJobId InferenceService::translateBatch(const BatchTranslationRequest &request) {
    NativeTranslationRequest native;
    native.source = request.source;
    native.target_language = request.target_language;
    native.source_language = request.source_language;
    TranslationJobId id;
    {
        std::lock_guard lock(mutex_);
        id = TranslationJobId{next_job_id_++};
        JobRecord record;
        record.id = id;
        record.request = native;
        record.batch = true;
        jobs_.emplace(id.value, std::move(record));
    }
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, id, native] { submitJob(id, native, TranslationChannel::Target, true); }, Qt::QueuedConnection);
    } else {
        submitJob(id, native, TranslationChannel::Target, true);
    }
    return id;
}

void InferenceService::submitJob(TranslationJobId id, const NativeTranslationRequest &request,
                                 TranslationChannel channel, bool batch) {
    bool cancelled = false;
    {
        std::lock_guard lock(mutex_);
        auto &job = jobs_[id.value];
        if (job.cancel_requested) {
            cancelled = true;
        } else {
            job.state = TranslationState::Running;
            job.active_channel = channel;
            job.running = true;
        }
    }
    if (cancelled) {
        finishJob(id, TranslationState::Cancelled);
        return;
    }

    std::string model_id;
    {
        std::lock_guard lock(mutex_);
        model_id = model_id_;
    }
    qtrans::core::TranslationInput input{request.source, language(request.source_language),
                                         language(request.target_language),
                                         request.wordselect ? qtrans::core::OverflowPolicy::Reject
                                                            : qtrans::core::OverflowPolicy::Split};
    qtrans::core::InvocationRequest core;
    core.model = {model_id};
    core.input = input;
    core.work_class = batch ? qtrans::core::WorkClass::Batch
                            : qtrans::core::WorkClass::NativeInteractive;
    const auto submitted = host_.submit(core, [this, id](const qtrans::core::InvocationEvent &event) {
        QMetaObject::invokeMethod(this, [this, id, event] { handleCoreEvent(id, event); }, Qt::QueuedConnection);
    });
    if (!submitted) {
        finishJob(id, TranslationState::Failed,
                  qtrans::app::from_utf8(submitted.failure.message));
        return;
    }
    std::lock_guard lock(mutex_);
    jobs_[id.value].handle = submitted.handle;
}

void InferenceService::handleCoreEvent(TranslationJobId id,
                                       const qtrans::core::InvocationEvent &event) {
    if (const auto *started = std::get_if<qtrans::core::InvocationStarted>(&event)) {
        Q_UNUSED(started);
        TranslationChannel channel = TranslationChannel::Target;
        {
            std::lock_guard lock(mutex_);
            const auto it = jobs_.find(id.value);
            if (it != jobs_.end()) channel = it->second.active_channel;
        }
        emit translationStarted(id);
        emit translationReset(id, channel);
    } else if (const auto *delta = std::get_if<qtrans::core::InvocationDelta>(&event)) {
        TranslationChannel channel = TranslationChannel::Target;
        {
            std::lock_guard lock(mutex_);
            const auto it = jobs_.find(id.value);
            if (it != jobs_.end()) channel = it->second.active_channel;
        }
        emit translationDelta(id, channel, qtrans::app::from_utf8(delta->text));
    } else if (const auto *finished = std::get_if<qtrans::core::InvocationFinished>(&event)) {
        const TranslationState state = mapFinishState(finished->result.finish_reason);
        bool start_back = false;
        NativeTranslationRequest back_request;
        {
            std::lock_guard lock(mutex_);
            auto &job = jobs_[id.value];
            job.running = false;
            if (job.cancel_requested) {
                start_back = false;
            } else if (job.back_translate && !job.back_started &&
                       state == TranslationState::Completed) {
                job.back_started = true;
                back_request.source = finished->result.output;
                back_request.source_language = job.request.target_language;
                back_request.target_language = job.request.source_language;
                start_back = true;
            }
        }
        if (start_back) {
            submitJob(id, back_request, TranslationChannel::BackTranslate, false);
            return;
        }
        if (state == TranslationState::Completed) {
            bool cancelled = false;
            {
                std::lock_guard lock(mutex_);
                cancelled = jobs_[id.value].cancel_requested;
            }
            if (cancelled) {
                finishJob(id, TranslationState::Cancelled);
                return;
            }
        }
        finishJob(id, state, finished->result.failure ? qtrans::app::from_utf8(finished->result.failure->message) : QString{});
    }
}

void InferenceService::finishJob(TranslationJobId id, TranslationState state,
                                 const QString &error) {
    {
        std::lock_guard lock(mutex_);
        auto &job = jobs_[id.value];
        job.state = state;
        job.running = false;
    }
    TranslationJobResult result;
    result.id = id;
    result.state = state;
    result.error_message = qtrans::app::to_utf8(error);
    emit translationFinished(result);
}

bool InferenceService::cancel(TranslationJobId id) {
    qtrans::core::InvocationHandle handle;
    {
        std::lock_guard lock(mutex_);
        const auto it = jobs_.find(id.value);
        if (it == jobs_.end()) return false;
        it->second.cancel_requested = true;
        if (it->second.running && it->second.handle) {
            handle = it->second.handle;
        } else {
            return true;
        }
    }
    return static_cast<bool>(handle.cancel());
}

bool InferenceService::preemptBatch() {
    qtrans::core::InvocationHandle handle;
    {
        std::lock_guard lock(mutex_);
        for (auto &[id, job] : jobs_) {
            Q_UNUSED(id);
            if (job.batch && job.running && job.handle) {
                handle = job.handle;
                break;
            }
        }
    }
    if (!handle) return false;
    return static_cast<bool>(host_.preempt(handle.id()));
}

TranslationState InferenceService::jobState(TranslationJobId id) const {
    std::lock_guard lock(mutex_);
    const auto it = jobs_.find(id.value);
    return it == jobs_.end() ? TranslationState::Failed : it->second.state;
}

bool InferenceService::isModelLoaded() const {
    return host_.snapshot().state == qtrans::core::LifecycleState::Ready;
}

QString InferenceService::backendLabel() const {
    return qtrans::app::from_utf8(qtrans::core::backend_state().label);
}

void InferenceService::loadModel() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, &InferenceService::loadModel, Qt::QueuedConnection);
        return;
    }
    std::string model_id, model_path;
    {
        std::lock_guard lock(mutex_);
        model_id = model_id_;
        model_path = model_path_;
    }
    emit statusChanged(QStringLiteral("Loading model into memory"), true);
    const auto result = host_.load({{model_id}, std::filesystem::u8path(model_path)});
    // Terminal nonbusy status for both success and failure so consumers can
    // never remain in a busy/disabled state after the lifecycle command.
    emit statusChanged(QStringLiteral("Ready"), false);
    emit modelLoadFinished(static_cast<bool>(result),
                           qtrans::app::from_utf8(result.failure.message), backendLabel());
}

void InferenceService::unloadModel() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, &InferenceService::unloadModel, Qt::QueuedConnection);
        return;
    }
    emit statusChanged(QStringLiteral("Unloading model"), true);
    const auto result = host_.unload();
    // Terminal nonbusy status for both success and failure.
    emit statusChanged(QStringLiteral("Ready"), false);
    if (result) emit modelUnloadFinished();
}

void InferenceService::shutdown() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, &InferenceService::shutdown,
                                  Qt::BlockingQueuedConnection);
        return;
    }
    if (host_.snapshot().state != qtrans::core::LifecycleState::Stopped) host_.shutdown();
}

void InferenceService::initializeBackend() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, &InferenceService::initializeBackend,
                                  Qt::BlockingQueuedConnection);
        return;
    }
    qtrans::core::BackendInitializationOptions options;
    options.diagnostic_sink = [](qtrans::core::DiagnosticLevel level,
                                 std::string_view component,
                                 std::string_view message) {
        const auto logger = qtrans::log::get(component == "llama"
                                                 ? qtrans::log::Component::Hymt
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
#ifndef NDEBUG
    options.trace_sink = [](std::string_view prompt, std::string_view response) {
        qtrans::log::write_ai_trace(std::string(prompt), std::string(response));
    };
#endif
    qtrans::core::configure_backend(options);
    RuntimeCapabilities::instance().refresh(qtrans::core::initialize_backend());
}

TranslationState InferenceService::mapFinishState(qtrans::core::FinishReason reason) {
    if (reason == qtrans::core::FinishReason::Completed || reason == qtrans::core::FinishReason::Length)
        return TranslationState::Completed;
    if (reason == qtrans::core::FinishReason::Preempted) return TranslationState::Preempted;
    if (reason == qtrans::core::FinishReason::Stop || reason == qtrans::core::FinishReason::Cancelled ||
        reason == qtrans::core::FinishReason::Deadline)
        return TranslationState::Cancelled;
    return TranslationState::Failed;
}
