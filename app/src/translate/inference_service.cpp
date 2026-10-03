#include "translate/inference_service.h"

#include "translate/api_chat.h"
#include "logging/ai_trace.h"
#include "logging/component.h"
#include "logging/logger.h"
#include "qtrans/runtime.h"
#include "shared/string_bridge.h"

#include <QMetaObject>
#include <QThread>

#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <mutex>
#include <unordered_map>
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

// Private implementation: owns the sole ModelHost plus all submission/tracking
// state. Defined only here so the public header cannot see the runtime types.
struct InferenceService::Impl {
    explicit Impl(InferenceService *owner_service)
        : owner(owner_service), host_({}) {
    }

    static constexpr std::size_t kTerminalHistoryLimit = 128;

    struct JobRecord {
        TranslationJobId id;
        TranslationState state = TranslationState::Pending;
        NativeTranslationRequest request;
        bool batch = false;
        bool back_translate = false;
        bool back_started = false;
        bool running = false;
        TranslationChannel active_channel = TranslationChannel::Target;
        qtrans::core::InvocationHandle handle;
        std::shared_ptr<TranslationCancellation> cancellation;
    };

    struct ApiChatRecord {
        ApiChatCallback callback;
        qtrans::core::InvocationHandle handle;
        bool running = false;
        bool cancel_requested = false;
    };

    void submitJob(TranslationJobId id, const NativeTranslationRequest &request,
                   TranslationChannel channel, bool batch);
    void handleCoreEvent(TranslationJobId id,
                         const qtrans::core::InvocationEvent &event);
    void finishJob(TranslationJobId id, TranslationState state,
                   const QString &error = {});
    void rememberTerminalJob(TranslationJobId id, TranslationState state);
    void publishRuntimeSnapshot(
        std::optional<qtrans::core::LifecycleState> lifecycle_override = std::nullopt);
    static TranslationState mapFinishState(qtrans::core::FinishReason reason);

    void doSubmitApiChat(std::uint64_t request_id, const ApiChatRequest &request);
    void handleApiChatEvent(std::uint64_t request_id,
                            const qtrans::core::InvocationEvent &event);
    void finishApiChat(std::uint64_t request_id,
                       const qtrans::core::InvocationResult &result);

    InferenceService *owner = nullptr;

    mutable std::mutex mutex_;
    std::uint64_t next_job_id_ = 1;
    std::unordered_map<std::uint64_t, JobRecord> jobs_;
    std::unordered_map<std::uint64_t, TranslationState> terminal_jobs_;
    std::deque<std::uint64_t> terminal_job_order_;
    std::string model_id_;
    std::string model_path_;
    qtrans::core::ModelHost host_;

    std::uint64_t next_api_request_id_ = 1;
    std::unordered_map<std::uint64_t, ApiChatRecord> api_chats_;
    std::unique_ptr<ApiChatBridge> api_chat_;
};

void InferenceService::Impl::submitJob(TranslationJobId id,
                                       const NativeTranslationRequest &request,
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
        QMetaObject::invokeMethod(owner, [this, id, event] { handleCoreEvent(id, event); }, Qt::QueuedConnection);
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

void InferenceService::Impl::handleCoreEvent(TranslationJobId id,
                                             const qtrans::core::InvocationEvent &event) {
    if (const auto *started = std::get_if<qtrans::core::InvocationStarted>(&event)) {
        Q_UNUSED(started);
        TranslationChannel channel = TranslationChannel::Target;
        {
            std::lock_guard lock(mutex_);
            const auto it = jobs_.find(id.value);
            if (it != jobs_.end()) channel = it->second.active_channel;
        }
        emit owner->translationStarted(id);
        emit owner->translationReset(id, channel);
    } else if (const auto *delta = std::get_if<qtrans::core::InvocationDelta>(&event)) {
        TranslationChannel channel = TranslationChannel::Target;
        {
            std::lock_guard lock(mutex_);
            const auto it = jobs_.find(id.value);
            if (it != jobs_.end()) channel = it->second.active_channel;
        }
        emit owner->translationDelta(id, channel, qtrans::app::from_utf8(delta->text));
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

void InferenceService::Impl::finishJob(TranslationJobId id, TranslationState state,
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
    emit owner->translationFinished(result);
    publishRuntimeSnapshot();
}

void InferenceService::Impl::rememberTerminalJob(TranslationJobId id,
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

void InferenceService::Impl::publishRuntimeSnapshot(
    std::optional<qtrans::core::LifecycleState> lifecycle_override) {
    if (QThread::currentThread() != owner->thread()) {
        QMetaObject::invokeMethod(
            owner,
            [this, lifecycle_override] {
                publishRuntimeSnapshot(lifecycle_override);
            },
            Qt::QueuedConnection);
        return;
    }
    auto snapshot = owner->runtimeSnapshot();
    if (lifecycle_override) snapshot.lifecycle = *lifecycle_override;
    emit owner->runtimeSnapshotChanged(snapshot);
}

TranslationState InferenceService::Impl::mapFinishState(qtrans::core::FinishReason reason) {
    if (reason == qtrans::core::FinishReason::Completed || reason == qtrans::core::FinishReason::Length)
        return TranslationState::Completed;
    if (reason == qtrans::core::FinishReason::Preempted) return TranslationState::Preempted;
    if (reason == qtrans::core::FinishReason::Stop || reason == qtrans::core::FinishReason::Cancelled ||
        reason == qtrans::core::FinishReason::Deadline)
        return TranslationState::Cancelled;
    return TranslationState::Failed;
}

void InferenceService::Impl::doSubmitApiChat(std::uint64_t request_id,
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
        QMetaObject::invokeMethod(owner, [this, request_id, event] { handleApiChatEvent(request_id, event); }, Qt::QueuedConnection);
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
    // submission (cancel() only cancels once a handle exists, so a cancel
    // observed here must be replayed). The handle is invoked outside the mutex.
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

void InferenceService::Impl::handleApiChatEvent(std::uint64_t request_id,
                                                const qtrans::core::InvocationEvent &event) {
    if (const auto *finished = std::get_if<qtrans::core::InvocationFinished>(&event)) {
        --g_api_trace_guard;
        finishApiChat(request_id, finished->result);
    }
}

void InferenceService::Impl::finishApiChat(std::uint64_t request_id,
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

InferenceService::InferenceService(QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>(this)) {
    qRegisterMetaType<TranslationJobId>("TranslationJobId");
    qRegisterMetaType<TranslationState>("TranslationState");
    qRegisterMetaType<TranslationChannel>("TranslationChannel");
    qRegisterMetaType<TranslationJobResult>("TranslationJobResult");
    qRegisterMetaType<RuntimeSnapshot>("RuntimeSnapshot");
    impl_->api_chat_.reset(new ApiChatBridge(this));
}

InferenceService::~InferenceService() {
    if (impl_->host_.snapshot().state != qtrans::core::LifecycleState::Stopped)
        impl_->host_.shutdown();
}

void InferenceService::setModelConfig(const QString &model_id, const QString &model_path) {
    std::lock_guard lock(impl_->mutex_);
    impl_->model_id_ = qtrans::app::to_utf8(model_id);
    impl_->model_path_ = qtrans::app::to_utf8(model_path);
}

TranslationJobId InferenceService::translateNative(const NativeTranslationRequest &request) {
    return submitNative(request).id;
}

TranslationJobTicket InferenceService::submitNative(
    const NativeTranslationRequest &request) {
    TranslationJobId id;
    auto cancellation = std::make_shared<TranslationCancellation>();
    {
        std::lock_guard lock(impl_->mutex_);
        id = TranslationJobId{impl_->next_job_id_++};
        Impl::JobRecord record;
        record.id = id;
        record.request = request;
        record.back_translate = request.back_translate;
        record.cancellation = cancellation;
        impl_->jobs_.emplace(id.value, std::move(record));
    }
    impl_->publishRuntimeSnapshot();
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, id, request] { impl_->submitJob(id, request, TranslationChannel::Target, false); }, Qt::QueuedConnection);
    } else {
        impl_->submitJob(id, request, TranslationChannel::Target, false);
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
        std::lock_guard lock(impl_->mutex_);
        id = TranslationJobId{impl_->next_job_id_++};
        Impl::JobRecord record;
        record.id = id;
        record.request = native;
        record.batch = true;
        record.cancellation = cancellation;
        impl_->jobs_.emplace(id.value, std::move(record));
    }
    impl_->publishRuntimeSnapshot();
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, id, native] { impl_->submitJob(id, native, TranslationChannel::Target, true); }, Qt::QueuedConnection);
    } else {
        impl_->submitJob(id, native, TranslationChannel::Target, true);
    }
    return {id, std::move(cancellation)};
}

bool InferenceService::cancel(TranslationJobId id) {
    std::shared_ptr<TranslationCancellation> cancellation;
    {
        std::lock_guard lock(impl_->mutex_);
        const auto it = impl_->jobs_.find(id.value);
        if (it == impl_->jobs_.end()) return false;
        cancellation = it->second.cancellation;
    }
    return cancellation->request();
}

bool InferenceService::preemptBatch() {
    qtrans::core::InvocationHandle handle;
    {
        std::lock_guard lock(impl_->mutex_);
        for (auto &[id, job] : impl_->jobs_) {
            Q_UNUSED(id);
            if (job.batch && job.running && job.handle) {
                handle = job.handle;
                break;
            }
        }
    }
    if (!handle) return false;
    return static_cast<bool>(impl_->host_.preempt(handle.id()));
}

TranslationState InferenceService::jobState(TranslationJobId id) const {
    std::lock_guard lock(impl_->mutex_);
    const auto it = impl_->jobs_.find(id.value);
    if (it != impl_->jobs_.end()) return it->second.state;
    const auto terminal = impl_->terminal_jobs_.find(id.value);
    return terminal == impl_->terminal_jobs_.end() ? TranslationState::Failed
                                                   : terminal->second;
}

RuntimeSnapshot InferenceService::runtimeSnapshot() const {
    const auto host_snapshot = impl_->host_.snapshot();
    RuntimeSnapshot snapshot;
    snapshot.lifecycle = host_snapshot.state;
    if (host_snapshot.model) snapshot.loaded_model_id = host_snapshot.model->value;
    snapshot.backend_label = qtrans::core::backend_state().label;
    snapshot.supports_conversation = host_snapshot.supports_conversation;
    {
        std::lock_guard lock(impl_->mutex_);
        snapshot.active_translation_jobs = impl_->jobs_.size();
        snapshot.active_api_jobs = impl_->api_chats_.size();
    }
    return snapshot;
}

bool InferenceService::isModelLoaded() const {
    return impl_->host_.snapshot().state == qtrans::core::LifecycleState::Ready;
}

ApiChatBridge *InferenceService::apiChat() {
    return impl_->api_chat_.get();
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
        std::lock_guard lock(impl_->mutex_);
        model_id = impl_->model_id_;
        model_path = impl_->model_path_;
    }
    emit statusChanged(QStringLiteral("Loading model into memory"), true);
    impl_->publishRuntimeSnapshot(qtrans::core::LifecycleState::Loading);
    const auto result = impl_->host_.load({{model_id}, std::filesystem::u8path(model_path)});
    // Terminal nonbusy status for both success and failure so consumers can
    // never remain in a busy/disabled state after the lifecycle command.
    emit statusChanged(QStringLiteral("Ready"), false);
    emit modelLoadFinished(static_cast<bool>(result),
                           qtrans::app::from_utf8(result.failure.message), backendLabel());
    impl_->publishRuntimeSnapshot();
}

void InferenceService::unloadModel() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, &InferenceService::unloadModel, Qt::QueuedConnection);
        return;
    }
    emit statusChanged(QStringLiteral("Unloading model"), true);
    impl_->publishRuntimeSnapshot(qtrans::core::LifecycleState::Unloading);
    const auto result = impl_->host_.unload();
    // Terminal nonbusy status and a terminal result for both success and
    // failure so consumers can never remain in a lifecycle busy state after
    // the unload attempt.
    emit statusChanged(QStringLiteral("Ready"), false);
    emit modelUnloadFinished(static_cast<bool>(result),
                             qtrans::app::from_utf8(result.failure.message));
    impl_->publishRuntimeSnapshot();
}

void InferenceService::shutdown() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, &InferenceService::shutdown,
                                  Qt::BlockingQueuedConnection);
        return;
    }
    impl_->publishRuntimeSnapshot(qtrans::core::LifecycleState::ShuttingDown);
    if (impl_->host_.snapshot().state != qtrans::core::LifecycleState::Stopped)
        impl_->host_.shutdown();
    impl_->publishRuntimeSnapshot();
}

qtrans::core::BackendState InferenceService::initializeBackend() {
    if (QThread::currentThread() != thread()) {
        qtrans::core::BackendState state;
        QMetaObject::invokeMethod(
            this, [this, &state] { state = initializeBackend(); },
            Qt::BlockingQueuedConnection);
        return state;
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
    return qtrans::core::initialize_backend();
}

ApiChatBridge::ApiChatBridge(InferenceService *service)
    : service_(service) {
}

bool ApiChatBridge::modelSnapshot(std::string *loaded_model_id,
                                  bool *supports_conversation) const {
    const auto snapshot = service_->impl_->host_.snapshot();
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

std::uint64_t ApiChatBridge::submit(const ApiChatRequest &request,
                                    ApiChatCallback callback) {
    auto *impl = service_->impl_.get();
    std::uint64_t request_id;
    {
        std::lock_guard lock(impl->mutex_);
        request_id = impl->next_api_request_id_++;
        InferenceService::Impl::ApiChatRecord record;
        record.callback = std::move(callback);
        impl->api_chats_.emplace(request_id, std::move(record));
    }
    impl->publishRuntimeSnapshot();
    if (QThread::currentThread() != service_->thread()) {
        QMetaObject::invokeMethod(service_, [impl, request_id, request] { impl->doSubmitApiChat(request_id, request); }, Qt::QueuedConnection);
    } else {
        impl->doSubmitApiChat(request_id, request);
    }
    return request_id;
}

bool ApiChatBridge::cancel(std::uint64_t request_id) {
    auto *impl = service_->impl_.get();
    qtrans::core::InvocationHandle handle;
    {
        std::lock_guard lock(impl->mutex_);
        const auto it = impl->api_chats_.find(request_id);
        if (it == impl->api_chats_.end()) return false;
        it->second.cancel_requested = true;
        if (it->second.running && it->second.handle) {
            handle = it->second.handle;
        } else {
            return true;
        }
    }
    return static_cast<bool>(handle.cancel());
}
