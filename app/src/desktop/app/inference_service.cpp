#include "app/inference_service.h"

#include "domain/inference/runtime_capabilities.h"
#include "domain/logging/ai_trace.h"
#include "domain/logging/component.h"
#include "domain/logging/logger.h"
#include "shared/string_bridge.h"

#include <QMetaObject>
#include <QThread>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <utility>

namespace {

qtrans::core::LanguageTag language(const std::string &value) {
    return {value.empty() ? "Auto" : value};
}

// Bounded latency for API chat invocations: generation that outlives this is
// stopped by the host and surfaces as a Deadline failure (mapped to HTTP 504
// by the local API service).
constexpr auto kApiGenerationDeadline = std::chrono::seconds(120);

// Global AI-trace suppression counter for API work. The backend trace sink is
// installed in core's global diagnostics configuration and must never capture
// an InferenceService pointer (use-after-free if the service outlives the
// backend, which it does at shutdown). This file-static atomic owns no object
// lifetime, and generation is serialized in the ModelHost, so the guard is
// accurate while API invocations are in flight.
std::atomic<int> g_api_trace_guard{0};

RuntimeLifecycleState map_lifecycle_state(
    qtrans::core::LifecycleState state) {
    switch (state) {
        case qtrans::core::LifecycleState::Unloaded:
            return RuntimeLifecycleState::Unloaded;
        case qtrans::core::LifecycleState::Loading:
            return RuntimeLifecycleState::Loading;
        case qtrans::core::LifecycleState::Ready:
            return RuntimeLifecycleState::Ready;
        case qtrans::core::LifecycleState::Unloading:
            return RuntimeLifecycleState::Unloading;
        case qtrans::core::LifecycleState::Draining:
            return RuntimeLifecycleState::Draining;
        case qtrans::core::LifecycleState::ShuttingDown:
            return RuntimeLifecycleState::ShuttingDown;
        case qtrans::core::LifecycleState::Stopped:
            return RuntimeLifecycleState::Stopped;
    }
    return RuntimeLifecycleState::Stopped;
}

#if defined(QTRANS_BUILD_TESTS)
std::function<void(TranslationJobId)> g_before_core_submit_hook;
#endif

}  // namespace

#if defined(QTRANS_BUILD_TESTS)
namespace inference_service_test {

void set_before_core_submit_hook(std::function<void(TranslationJobId)> hook) {
    g_before_core_submit_hook = std::move(hook);
}

}  // namespace inference_service_test
#endif

InferenceService::InferenceService(QObject *parent)
    : QObject(parent), host_({}) {
    qRegisterMetaType<TranslationJobId>("TranslationJobId");
    qRegisterMetaType<TranslationState>("TranslationState");
    qRegisterMetaType<TranslationChannel>("TranslationChannel");
    qRegisterMetaType<TranslationJobResult>("TranslationJobResult");
    qRegisterMetaType<RuntimeSnapshot>("RuntimeSnapshot");
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
    return submitNative(request).id;
}

TranslationJobTicket InferenceService::submitNative(
    const NativeTranslationRequest &request) {
    TranslationJobId id;
    auto cancellation = std::make_shared<TranslationCancellation>();
    {
        std::lock_guard lock(mutex_);
        id = TranslationJobId{next_job_id_++};
        JobRecord record;
        record.id = id;
        record.request = request;
        record.back_translate = request.back_translate;
        record.cancellation = cancellation;
        jobs_.emplace(id.value, std::move(record));
    }
    publishRuntimeSnapshot();
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, id, request] { submitJob(id, request, TranslationChannel::Target, false); }, Qt::QueuedConnection);
    } else {
        submitJob(id, request, TranslationChannel::Target, false);
    }
    return {id, std::move(cancellation)};
}

TranslationJobId InferenceService::translateBatch(const BatchTranslationRequest &request) {
    return submitBatch(request).id;
}

TranslationJobTicket InferenceService::submitBatch(
    const BatchTranslationRequest &request) {
    NativeTranslationRequest native;
    native.source = request.source;
    native.target_language = request.target_language;
    native.source_language = request.source_language;
    TranslationJobId id;
    auto cancellation = std::make_shared<TranslationCancellation>();
    {
        std::lock_guard lock(mutex_);
        id = TranslationJobId{next_job_id_++};
        JobRecord record;
        record.id = id;
        record.request = native;
        record.batch = true;
        record.cancellation = cancellation;
        jobs_.emplace(id.value, std::move(record));
    }
    publishRuntimeSnapshot();
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, id, native] { submitJob(id, native, TranslationChannel::Target, true); }, Qt::QueuedConnection);
    } else {
        submitJob(id, native, TranslationChannel::Target, true);
    }
    return {id, std::move(cancellation)};
}

void InferenceService::submitJob(TranslationJobId id, const NativeTranslationRequest &request,
                                 TranslationChannel channel, bool batch) {
    bool cancelled = false;
    {
        std::lock_guard lock(mutex_);
        auto &job = jobs_[id.value];
        if (job.cancellation->requested()) {
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
#if defined(QTRANS_BUILD_TESTS)
    if (g_before_core_submit_hook) g_before_core_submit_hook(id);
#endif
    const auto submitted = host_.submit(core, [this, id](const qtrans::core::InvocationEvent &event) {
        QMetaObject::invokeMethod(this, [this, id, event] { handleCoreEvent(id, event); }, Qt::QueuedConnection);
    });
    if (!submitted) {
        finishJob(id, TranslationState::Failed,
                  qtrans::app::from_utf8(submitted.failure.message));
        return;
    }
    // Install the core cancellation callback into the ticket. install()
    // atomically replays a cancellation requested during submission.
    std::shared_ptr<TranslationCancellation> cancellation;
    {
        std::lock_guard lock(mutex_);
        const auto it = jobs_.find(id.value);
        if (it == jobs_.end()) return;
        it->second.handle = submitted.handle;
        cancellation = it->second.cancellation;
    }
    cancellation->install([handle = submitted.handle] { handle.cancel(); });
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
            const auto found = jobs_.find(id.value);
            if (found == jobs_.end()) return;
            auto &job = found->second;
            job.running = false;
            if (job.cancellation->requested()) {
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
                const auto found = jobs_.find(id.value);
                cancelled = found != jobs_.end() &&
                            found->second.cancellation->requested();
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
        const auto found = jobs_.find(id.value);
        if (found == jobs_.end()) return;
        found->second.cancellation->complete();
        jobs_.erase(found);
        rememberTerminalJob(id, state);
    }
    TranslationJobResult result;
    result.id = id;
    result.state = state;
    result.error_message = qtrans::app::to_utf8(error);
    emit translationFinished(result);
    publishRuntimeSnapshot();
}

void InferenceService::rememberTerminalJob(TranslationJobId id,
                                           TranslationState state) {
    // mutex_ is held by the caller. Terminal history intentionally contains no
    // request text or core handles and is bounded for long-running sessions.
    terminal_jobs_[id.value] = state;
    terminal_job_order_.push_back(id.value);
    while (terminal_job_order_.size() > kTerminalHistoryLimit) {
        terminal_jobs_.erase(terminal_job_order_.front());
        terminal_job_order_.pop_front();
    }
}

bool InferenceService::cancel(TranslationJobId id) {
    std::shared_ptr<TranslationCancellation> cancellation;
    {
        std::lock_guard lock(mutex_);
        const auto it = jobs_.find(id.value);
        if (it == jobs_.end()) return false;
        cancellation = it->second.cancellation;
    }
    return cancellation->request();
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
    if (it != jobs_.end()) return it->second.state;
    const auto terminal = terminal_jobs_.find(id.value);
    return terminal == terminal_jobs_.end() ? TranslationState::Failed
                                            : terminal->second;
}

RuntimeSnapshot InferenceService::runtimeSnapshot() const {
    const auto host_snapshot = host_.snapshot();
    RuntimeSnapshot snapshot;
    snapshot.lifecycle = map_lifecycle_state(host_snapshot.state);
    if (host_snapshot.model) snapshot.loaded_model_id = host_snapshot.model->value;
    snapshot.backend_label = qtrans::core::backend_state().label;
    snapshot.supports_conversation = host_snapshot.supports_conversation;
    {
        std::lock_guard lock(mutex_);
        snapshot.active_translation_jobs = jobs_.size();
        snapshot.active_api_jobs = api_chats_.size();
    }
    return snapshot;
}

void InferenceService::publishRuntimeSnapshot(
    std::optional<RuntimeLifecycleState> lifecycle_override) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this,
            [this, lifecycle_override] {
                publishRuntimeSnapshot(lifecycle_override);
            },
            Qt::QueuedConnection);
        return;
    }
    auto snapshot = runtimeSnapshot();
    if (lifecycle_override) snapshot.lifecycle = *lifecycle_override;
    emit runtimeSnapshotChanged(snapshot);
}

bool InferenceService::isModelLoaded() const {
    return host_.snapshot().state == qtrans::core::LifecycleState::Ready;
}

bool InferenceService::apiModelSnapshot(std::string *loaded_model_id,
                                        bool *supports_conversation) const {
    const auto snapshot = host_.snapshot();
    if (snapshot.state != qtrans::core::LifecycleState::Ready || !snapshot.model) {
        return false;
    }
    if (loaded_model_id != nullptr) {
        *loaded_model_id = snapshot.model->value;
    }
    if (supports_conversation != nullptr) {
        *supports_conversation = snapshot.supports_conversation;
    }
    return true;
}

std::uint64_t InferenceService::submitApiChat(const ApiChatRequest &request,
                                              ApiChatCallback callback) {
    std::uint64_t request_id;
    {
        std::lock_guard lock(mutex_);
        request_id = next_api_request_id_++;
        ApiChatRecord record;
        record.callback = std::move(callback);
        api_chats_.emplace(request_id, std::move(record));
    }
    publishRuntimeSnapshot();
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, request_id, request] { doSubmitApiChat(request_id, request); }, Qt::QueuedConnection);
    } else {
        doSubmitApiChat(request_id, request);
    }
    return request_id;
}

void InferenceService::doSubmitApiChat(std::uint64_t request_id,
                                       const ApiChatRequest &request) {
    ApiChatCallback callback;
    bool cancelled = false;
    {
        std::lock_guard lock(mutex_);
        const auto it = api_chats_.find(request_id);
        if (it == api_chats_.end()) return;
        if (it->second.cancel_requested) {
            cancelled = true;
        } else {
            it->second.running = true;
        }
    }
    if (cancelled) {
        {
            std::lock_guard lock(mutex_);
            auto it = api_chats_.find(request_id);
            if (it == api_chats_.end()) return;
            callback = std::move(it->second.callback);
            api_chats_.erase(it);
        }
        if (callback) {
            qtrans::core::InvocationResult result;
            result.finish_reason = qtrans::core::FinishReason::Cancelled;
            callback(ApiChatReply{false,
                                  {qtrans::core::FailureCode::Cancelled, "request cancelled"},
                                  result});
        }
        return;
    }

    ++g_api_trace_guard;
    qtrans::core::InvocationRequest core;
    core.model = {request.model_id};
    core.input = qtrans::core::ConversationInput{request.messages};
    qtrans::core::SamplingOptions sampling;
    if (request.temperature) sampling.temperature = *request.temperature;
    if (request.top_p) sampling.top_p = *request.top_p;
    if (request.seed) sampling.seed = *request.seed;
    if (request.max_output_tokens) sampling.max_output_tokens = *request.max_output_tokens;
    core.sampling = sampling;
    core.work_class = qtrans::core::WorkClass::ApiInteractive;
    core.deadline = std::chrono::steady_clock::now() + kApiGenerationDeadline;
    core.client_request_id = std::to_string(request_id);

    const auto submitted = host_.submit(core, [this, request_id](const qtrans::core::InvocationEvent &event) {
        QMetaObject::invokeMethod(this, [this, request_id, event] { handleApiChatEvent(request_id, event); }, Qt::QueuedConnection);
    });
    if (!submitted) {
        --g_api_trace_guard;
        {
            std::lock_guard lock(mutex_);
            auto it = api_chats_.find(request_id);
            if (it == api_chats_.end()) return;
            callback = std::move(it->second.callback);
            api_chats_.erase(it);
        }
        if (callback) callback(ApiChatReply{false, submitted.failure, {}});
        return;
    }
    // Install the handle, then honor any cancellation that raced the
    // submission (cancelApiChat() only cancels once a handle exists, so a
    // cancel observed here must be replayed). The handle is invoked outside
    // the mutex.
    qtrans::core::InvocationHandle handle;
    bool cancel_after_install = false;
    {
        std::lock_guard lock(mutex_);
        const auto it = api_chats_.find(request_id);
        if (it != api_chats_.end()) {
            it->second.handle = submitted.handle;
            cancel_after_install = it->second.cancel_requested;
            if (cancel_after_install) handle = it->second.handle;
        }
    }
    if (cancel_after_install) handle.cancel();
}

void InferenceService::handleApiChatEvent(std::uint64_t request_id,
                                          const qtrans::core::InvocationEvent &event) {
    if (const auto *finished = std::get_if<qtrans::core::InvocationFinished>(&event)) {
        --g_api_trace_guard;
        finishApiChat(request_id, finished->result);
    }
}

void InferenceService::finishApiChat(std::uint64_t request_id,
                                     const qtrans::core::InvocationResult &result) {
    ApiChatCallback callback;
    {
        std::lock_guard lock(mutex_);
        auto it = api_chats_.find(request_id);
        if (it == api_chats_.end()) return;
        callback = std::move(it->second.callback);
        api_chats_.erase(it);
    }
    publishRuntimeSnapshot();
    if (callback) callback(ApiChatReply{true, result.failure.value_or(qtrans::core::Failure{}), result});
}

bool InferenceService::cancelApiChat(std::uint64_t request_id) {
    qtrans::core::InvocationHandle handle;
    {
        std::lock_guard lock(mutex_);
        const auto it = api_chats_.find(request_id);
        if (it == api_chats_.end()) return false;
        it->second.cancel_requested = true;
        if (it->second.running && it->second.handle) {
            handle = it->second.handle;
        } else {
            return true;
        }
    }
    return static_cast<bool>(handle.cancel());
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
    publishRuntimeSnapshot(RuntimeLifecycleState::Loading);
    const auto result = host_.load({{model_id}, std::filesystem::u8path(model_path)});
    // Terminal nonbusy status for both success and failure so consumers can
    // never remain in a busy/disabled state after the lifecycle command.
    emit statusChanged(QStringLiteral("Ready"), false);
    emit modelLoadFinished(static_cast<bool>(result),
                           qtrans::app::from_utf8(result.failure.message), backendLabel());
    publishRuntimeSnapshot();
}

void InferenceService::unloadModel() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, &InferenceService::unloadModel, Qt::QueuedConnection);
        return;
    }
    emit statusChanged(QStringLiteral("Unloading model"), true);
    publishRuntimeSnapshot(RuntimeLifecycleState::Unloading);
    const auto result = host_.unload();
    // Terminal nonbusy status and a terminal result for both success and
    // failure so consumers can never remain in a lifecycle busy state after
    // the unload attempt.
    emit statusChanged(QStringLiteral("Ready"), false);
    emit modelUnloadFinished(static_cast<bool>(result),
                             qtrans::app::from_utf8(result.failure.message));
    publishRuntimeSnapshot();
}

void InferenceService::shutdown() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, &InferenceService::shutdown,
                                  Qt::BlockingQueuedConnection);
        return;
    }
    publishRuntimeSnapshot(RuntimeLifecycleState::ShuttingDown);
    if (host_.snapshot().state != qtrans::core::LifecycleState::Stopped) host_.shutdown();
    publishRuntimeSnapshot();
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
    // API requests never produce prompt/response trace files; only native
    // desktop work does. The sink is stored in core's global diagnostics
    // configuration and therefore must not capture any InferenceService state:
    // it reads only the file-static guard (see g_api_trace_guard above).
    options.trace_sink = [](std::string_view prompt, std::string_view response) {
        if (g_api_trace_guard.load(std::memory_order_relaxed) > 0) return;
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
