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

class SessionController : public QObject {
    Q_OBJECT

public:
    SessionController(HotkeyManager *hotkeyMgr, InferenceService *inferenceService,
                      PopupWindow *popup, QObject *parent = nullptr);

    void initialize();
    void setHotkey(const QString &shortcut);
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

private slots:
    void doTranslate();

private:
    bool checkDebounce();
    void resetSession();

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
};
