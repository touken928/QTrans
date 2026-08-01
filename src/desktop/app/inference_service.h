#pragma once

#include "domain/inference/inference_types.h"
#include "qtrans/core.h"

#include <QMetaType>
#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

Q_DECLARE_METATYPE(TranslationJobId)
Q_DECLARE_METATYPE(TranslationState)
Q_DECLARE_METATYPE(TranslationChannel)
Q_DECLARE_METATYPE(TranslationJobResult)

// Sole ModelHost owner for desktop inference. Lives on the worker thread and
// adapts the core ModelHost invocation domain to typed desktop translation
// jobs: model load/unload, native (main + popup) and batch translation,
// forward/back workflow with sticky cancellation, batch preemption, and all
// ModelHost -> Qt queued event adaptation. No other desktop object owns a
// ModelHost.
class InferenceService : public QObject {
    Q_OBJECT

public:
    explicit InferenceService(QObject *parent = nullptr);
    ~InferenceService() override;

    InferenceService(const InferenceService &) = delete;
    InferenceService &operator=(const InferenceService &) = delete;

    // Model configuration consumed by the next loadModel().
    void setModelConfig(const QString &model_id, const QString &model_path);

    // ── Typed translation entry points ───────────────────────────────────
    // Returns the job id synchronously; core submission is scheduled on the
    // service thread. Safe from any thread.
    TranslationJobId translateNative(const NativeTranslationRequest &request);
    TranslationJobId translateBatch(const BatchTranslationRequest &request);

    // ── Model lifecycle ──────────────────────────────────────────────────
    void loadModel();
    void unloadModel();
    bool isModelLoaded() const;
    QString backendLabel() const;

    // ── Cancellation / preemption (thread-safe) ──────────────────────────
    bool cancel(TranslationJobId id);
    bool preemptBatch();
    TranslationState jobState(TranslationJobId id) const;

    // ── Local OpenAI-compatible API bridge ───────────────────────────────
    // Safe from any thread. Submission is scheduled on the service thread;
    // the callback is invoked on the service thread with the terminal result
    // or a submission failure. The service never loads/unloads or otherwise
    // changes the model; it only observes the actual loaded model and submits
    // ApiInteractive conversation work against it. Every admitted invocation
    // carries a bounded generation deadline (see kApiGenerationDeadline in
    // inference_service.cpp); deadline expiry surfaces as a Deadline failure.
    struct ApiChatRequest {
        std::string model_id;
        std::vector<qtrans::core::Message> messages;
        std::optional<float> temperature;
        std::optional<float> top_p;
        std::optional<std::uint32_t> seed;
        std::optional<std::uint32_t> max_output_tokens;
    };

    struct ApiChatReply {
        bool accepted = false;
        qtrans::core::Failure failure;
        qtrans::core::InvocationResult result;
    };

    using ApiChatCallback = std::function<void(const ApiChatReply &)>;

    // Snapshots the currently Ready loaded model (never the configured "next"
    // model). Returns false and leaves the out params untouched when the host
    // is not Ready. When true, *loaded_model_id is the exact loaded id and
    // *supports_conversation reports whether that model's prompt profile
    // accepts ConversationInput (both optional out params).
    bool apiModelSnapshot(std::string *loaded_model_id = nullptr,
                          bool *supports_conversation = nullptr) const;
    // Submits an API chat request; returns a request id synchronously. The
    // callback is invoked exactly once with the terminal outcome.
    std::uint64_t submitApiChat(const ApiChatRequest &request, ApiChatCallback callback);
    // Cancels a pending/running API request by id. Returns true when the id
    // was known; idempotent for unknown ids.
    bool cancelApiChat(std::uint64_t request_id);

    // ── Lifecycle ────────────────────────────────────────────────────────
    void shutdown();
    void initializeBackend();

signals:
    // Emitted on the service thread; consumers on other threads receive them
    // via queued connections.
    void statusChanged(const QString &message, bool busy);
    void modelLoadFinished(bool success, const QString &error_message,
                           const QString &backend_label);
    void modelUnloadFinished();
    void translationStarted(TranslationJobId id);
    void translationReset(TranslationJobId id, TranslationChannel channel);
    void translationDelta(TranslationJobId id, TranslationChannel channel,
                          const QString &piece);
    void translationFinished(TranslationJobResult result);

private:
    struct JobRecord {
        TranslationJobId id;
        TranslationState state = TranslationState::Pending;
        NativeTranslationRequest request;
        bool batch = false;
        bool back_translate = false;
        bool back_started = false;
        bool cancel_requested = false;
        bool running = false;
        TranslationChannel active_channel = TranslationChannel::Target;
        qtrans::core::InvocationHandle handle;
    };

    void submitJob(TranslationJobId id, const NativeTranslationRequest &request,
                   TranslationChannel channel, bool batch);
    void handleCoreEvent(TranslationJobId id,
                         const qtrans::core::InvocationEvent &event);
    void finishJob(TranslationJobId id, TranslationState state,
                   const QString &error = {});
    static TranslationState mapFinishState(qtrans::core::FinishReason reason);

    void doSubmitApiChat(std::uint64_t request_id, const ApiChatRequest &request);
    void handleApiChatEvent(std::uint64_t request_id,
                            const qtrans::core::InvocationEvent &event);
    void finishApiChat(std::uint64_t request_id, const qtrans::core::InvocationResult &result);

    mutable std::mutex mutex_;
    std::uint64_t next_job_id_ = 1;
    std::unordered_map<std::uint64_t, JobRecord> jobs_;
    std::string model_id_;
    std::string model_path_;
    qtrans::core::ModelHost host_;

    struct ApiChatRecord {
        ApiChatCallback callback;
        qtrans::core::InvocationHandle handle;
        bool running = false;
        bool cancel_requested = false;
    };
    std::uint64_t next_api_request_id_ = 1;
    std::unordered_map<std::uint64_t, ApiChatRecord> api_chats_;
};
