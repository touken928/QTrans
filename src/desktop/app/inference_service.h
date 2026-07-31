#pragma once

#include "domain/inference/inference_types.h"
#include "qtrans/core.h"

#include <QMetaType>
#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

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

    mutable std::mutex mutex_;
    std::uint64_t next_job_id_ = 1;
    std::unordered_map<std::uint64_t, JobRecord> jobs_;
    std::string model_id_;
    std::string model_path_;
    qtrans::core::ModelHost host_;
};
