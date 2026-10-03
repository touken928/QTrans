#pragma once

#include "translate/inference_types.h"
#include "qtrans/backend.h"

#include <QMetaType>
#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>

Q_DECLARE_METATYPE(TranslationJobId)
Q_DECLARE_METATYPE(TranslationState)
Q_DECLARE_METATYPE(TranslationChannel)
Q_DECLARE_METATYPE(TranslationJobResult)
Q_DECLARE_METATYPE(RuntimeSnapshot)

class ApiChatBridge;

// Sole ModelHost owner for desktop inference. Lives on the worker thread and
// adapts the core ModelHost invocation domain to typed desktop translation
// jobs: model load/unload, native (main + popup) and batch translation,
// forward/back workflow with sticky cancellation, batch preemption, and all
// ModelHost -> Qt queued event adaptation. No other desktop object owns a
// ModelHost. The host lives in a private Impl so the ModelHost boundary stays
// hidden from consumers; API chat is delegated to ApiChatBridge.
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
    TranslationJobTicket submitNative(const NativeTranslationRequest &request);
    TranslationJobTicket submitBatch(const BatchTranslationRequest &request);

    // ── Model lifecycle ──────────────────────────────────────────────────
    void loadModel();
    void unloadModel();
    bool isModelLoaded() const;
    QString backendLabel() const;

    // ── Cancellation / preemption (thread-safe) ──────────────────────────
    bool cancel(TranslationJobId id);
    bool preemptBatch();
    TranslationState jobState(TranslationJobId id) const;
    RuntimeSnapshot runtimeSnapshot() const;

    // ── Local OpenAI-compatible API bridge ───────────────────────────────
    // Stable for the service lifetime. The bridge never loads/unloads or
    // otherwise changes the model; it only observes the actual loaded model
    // and submits ApiInteractive conversation work against it.
    ApiChatBridge *apiChat();

    // ── Lifecycle ────────────────────────────────────────────────────────
    void shutdown();
    qtrans::core::BackendState initializeBackend();

signals:
    // Emitted on the service thread; consumers on other threads receive them
    // via queued connections.
    void statusChanged(const QString &message, bool busy);
    void modelLoadFinished(bool success, const QString &error_message,
                           const QString &backend_label);
    // Terminal for both success and failure: consumers must never remain in
    // a lifecycle busy state after an unload attempt.
    void modelUnloadFinished(bool success, const QString &error_message);
    void translationStarted(TranslationJobId id);
    void translationReset(TranslationJobId id, TranslationChannel channel);
    void translationDelta(TranslationJobId id, TranslationChannel channel,
                          const QString &piece);
    void translationFinished(TranslationJobResult result);
    void runtimeSnapshotChanged(RuntimeSnapshot snapshot);

private:
    friend class ApiChatBridge;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
