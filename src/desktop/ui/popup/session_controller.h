#pragma once

#include "domain/inference/inference_types.h"

#include <QObject>
#include <QString>
#include <chrono>

enum class PopupState {
    Idle,
    CaptureScheduled,
    Capturing,
    Translating,
    Showing
};

class PopupWindow;
class InferenceService;
class HotkeyManager;

// Glues the word-select hotkey, clipboard capture, inference job, and the
// result popup into one session. The session retains the submitted request
// so a failed job can be retried from the popup with the exact same
// source/languages; job-id filtering keeps unrelated (main-window or batch)
// jobs out of the popup.
//
// The lifecycle is a set of explicit phases (Idle/CaptureScheduled/
// Capturing/Translating/Showing) guarded by a monotonically increasing
// session token. Clipboard capture pumps the Qt event loop, so a dismissal
// or a newer hotkey can run reentrantly while a capture is in flight; every
// session boundary (dismissal, supersede, retry, new capture) bumps the
// token first, and timer callbacks plus job signals reject work that no
// longer belongs to the current session. A stale capture can therefore
// never submit a translation.
class SessionController : public QObject {
    Q_OBJECT

public:
    SessionController(HotkeyManager *hotkeyMgr, InferenceService *inferenceService,
                      PopupWindow *popup, QObject *parent = nullptr);

    void initialize();
    // Registers `shortcut` and returns whether it is now active. An empty
    // shortcut unregisters (returns true). A shortcut equal to the active
    // registration is a safe no-op. On failure the previous registration is
    // restored (rollback) and hotkey() keeps reporting whatever binding is
    // actually active (the old shortcut, or nothing if the rollback also
    // failed), so a failed shortcut is never persisted by callers.
    bool setHotkey(const QString &shortcut);
    QString hotkey() const;

    void setEnabled(bool enabled);
    bool isEnabled() const;

    void setTranslateLanguages(const QString &source, const QString &target);

    int translateHotkeyId() const {
        return m_translateHotkeyId;
    }

public slots:
    void onHotkeyTriggered(int hotkeyId);
    void onTranslationStarted(TranslationJobId jobId);
    void onTranslationDelta(TranslationJobId jobId, TranslationChannel channel,
                            const QString &piece);
    void onTranslationFinished(const TranslationJobResult &result);
    void onPopupDismissed();
    void onRetryRequested();

private slots:
    void doTranslate();

private:
    bool checkDebounce();
    // Bumps the session token, invalidating any pending capture or job from
    // an earlier session. Returns the new token.
    std::uint64_t invalidateSession();
    // Cancels the in-flight job, if the session is currently Translating
    // with a valid job id. Returns whether a job was cancelled.
    bool cancelActiveJob();
    void resetSession();
    void submitRequest(const NativeTranslationRequest &request,
                       const QString &sourceText);

    PopupState m_state = PopupState::Idle;
    // Monotonically increasing session generation. Bumped at every session
    // boundary (dismissal, supersede, new capture, retry, reset) before the
    // boundary takes effect; stale timer callbacks and job signals compare
    // against it and reject themselves.
    std::uint64_t m_sessionToken = 0;
    // Token of the session that submitted the active job. Job signals must
    // match it in addition to the job id before they are accepted.
    std::uint64_t m_jobToken = 0;
    bool m_enabled = true;
    int m_translateHotkeyId = 1;
    int m_debounceMs = 800;
    std::chrono::steady_clock::time_point m_lastTrigger;

    HotkeyManager *m_hotkeyManager = nullptr;
    InferenceService *m_inferenceService = nullptr;
    PopupWindow *m_popup = nullptr;

    TranslationJobId m_activeJobId{};
    QString m_hotkeyStr;
    QString m_sourceLanguage;
    QString m_targetLanguage;

    // Retained request for the popup Retry affordance. Cleared when the
    // session resets (popup dismissed or a new capture begins).
    NativeTranslationRequest m_lastRequest{};
    QString m_lastSourceText;
    bool m_hasLastRequest = false;
};
