#pragma once

#include "domain/inference/inference_types.h"

#include <QObject>
#include <QString>
#include <chrono>

enum class PopupState {
    Idle,
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
    void resetSession();
    void submitRequest(const NativeTranslationRequest &request,
                       const QString &sourceText);

    PopupState m_state = PopupState::Idle;
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
