#pragma once

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
class TaskService;
class HotkeyManager;

class SessionController : public QObject
{
    Q_OBJECT

public:
    SessionController(HotkeyManager* hotkeyMgr, TaskService* taskService,
                      PopupWindow* popup, QObject* parent = nullptr);

    void initialize();
    void setHotkey(const QString& shortcut);
    QString hotkey() const;

    void setEnabled(bool enabled);
    bool isEnabled() const;

    void setTranslateLanguages(const QString & source, const QString & target);

    int translateHotkeyId() const { return m_translateHotkeyId; }

public slots:
    void onHotkeyTriggered(int hotkeyId);
    void onTranslateTaskStarted(quint64 taskId);
    void onTargetReset(quint64 taskId);
    void onTargetAppended(quint64 taskId, const QString& piece);
    void onTranslationFinished(quint64 taskId, int state);
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

    HotkeyManager* m_hotkeyManager = nullptr;
    TaskService* m_taskService = nullptr;
    PopupWindow* m_popup = nullptr;

    quint64 m_activeTaskId = 0;
    QString m_hotkeyStr;
    QString m_sourceLanguage;
    QString m_targetLanguage;
};
