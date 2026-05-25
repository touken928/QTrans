#include "wordselect/session_controller.h"

#include "app/task_service.h"
#include "wordselect/clipboard_capture.h"
#include "wordselect/hotkey_manager.h"
#include "wordselect/popup_window.h"

#include <QMetaObject>
#include <QTimer>
#include <cstdio>

#ifdef Q_OS_MACOS
#include "wordselect/mac/platform_utils.h"
#endif

SessionController::SessionController(
    HotkeyManager* hotkeyMgr,
    TaskService* taskService,
    PopupWindow* popup,
    QObject* parent)
    : QObject(parent)
    , m_hotkeyManager(hotkeyMgr)
    , m_taskService(taskService)
    , m_popup(popup) {

    connect(m_hotkeyManager, &HotkeyManager::hotkeyTriggered,
            this, &SessionController::onHotkeyTriggered);

    connect(m_taskService, &TaskService::translateTaskStarted,
            this, &SessionController::onTranslateTaskStarted);
    connect(m_taskService, &TaskService::targetReset,
            this, &SessionController::onTargetReset);
    connect(m_taskService, &TaskService::targetAppended,
            this, &SessionController::onTargetAppended);
    connect(m_taskService, &TaskService::translationFinished,
            this, &SessionController::onTranslationFinished);

    connect(m_popup, &PopupWindow::dismissed,
            this, &SessionController::onPopupDismissed);
}

void SessionController::initialize() {
    m_hotkeyStr = QStringLiteral("Ctrl+`");
    setHotkey(m_hotkeyStr);
    m_sourceLanguage = QStringLiteral("Auto");
    m_targetLanguage = QStringLiteral("Chinese");
}

void SessionController::setTranslateLanguages(const QString & source, const QString & target) {
    m_sourceLanguage = source;
    m_targetLanguage = target;
}

void SessionController::setHotkey(const QString& shortcut) {
    m_hotkeyStr = shortcut.trimmed();
    const QKeySequence ks(m_hotkeyStr);
    if (ks.isEmpty()) {
        m_hotkeyManager->unregisterHotkey(m_translateHotkeyId);
        return;
    }

    const Qt::KeyboardModifiers mods = ks[0].keyboardModifiers();
    const Qt::Key key = static_cast<Qt::Key>(ks[0].key());

    m_hotkeyManager->unregisterHotkey(m_translateHotkeyId);
    m_hotkeyManager->registerHotkey(m_translateHotkeyId, mods, key);
}

QString SessionController::hotkey() const {
    return m_hotkeyStr;
}

void SessionController::setEnabled(bool enabled) {
    m_enabled = enabled;
}

bool SessionController::isEnabled() const {
    return m_enabled;
}

void SessionController::onHotkeyTriggered(int hotkeyId) {
    if (hotkeyId != m_translateHotkeyId || !m_enabled) {
        return;
    }

    if (!checkDebounce()) {
        return;
    }

    fprintf(stderr, "[SessionCtrl] hotkey triggered, state=%d\n", static_cast<int>(m_state));

    if (m_state == PopupState::Capturing) {
        fprintf(stderr, "[SessionCtrl] already capturing, ignoring\n");
        return;
    }

    if (m_state == PopupState::Translating) {
        fprintf(stderr, "[SessionCtrl] cancelling current translation\n");
        if (m_activeTaskId != 0) {
            TaskId id{};
            id.value = m_activeTaskId;
            m_taskService->cancel(id);
        }
        resetSession();
    }

    if (!m_taskService->isModelLoaded()) {
        fprintf(stderr, "[SessionCtrl] model not loaded\n");
        m_popup->showError(QStringLiteral("Model not loaded. Open main window and load a model first."));
#ifdef Q_OS_MACOS
        macRestoreFrontApp();
#endif
        return;
    }

    m_state = PopupState::Capturing;
#ifdef Q_OS_MACOS
    macSaveFrontApp();
#endif
    QTimer::singleShot(50, this, &SessionController::doTranslate);
}

void SessionController::doTranslate() {
    if (m_state != PopupState::Capturing) {
        fprintf(stderr, "[SessionCtrl] doTranslate: not capturing (state=%d), skipping\n",
                static_cast<int>(m_state));
        return;
    }

    fprintf(stderr, "[SessionCtrl] capturing clipboard text\n");
    const QString text = ClipboardCapture::captureSelectedText(300);
    if (text.isEmpty()) {
        fprintf(stderr, "[SessionCtrl] captured text is empty, resetting session\n");
#ifdef Q_OS_MACOS
        macRestoreFrontApp();
#endif
        resetSession();
        return;
    }

    fprintf(stderr, "[SessionCtrl] captured text: '%s' (len=%d)\n",
            text.toUtf8().constData(), static_cast<int>(text.size()));

    m_state = PopupState::Translating;
    m_activeTaskId = 0;

    QMetaObject::invokeMethod(
        m_taskService,
        "translateInteractive",
        Qt::QueuedConnection,
        Q_ARG(QString, text),
        Q_ARG(QString, m_targetLanguage),
        Q_ARG(QString, m_sourceLanguage),
        Q_ARG(bool, false));
}

void SessionController::onTranslateTaskStarted(quint64 taskId) {
    if (m_state != PopupState::Translating) {
        return;
    }

    m_activeTaskId = taskId;
    m_popup->showLoading(QString());
#ifdef Q_OS_MACOS
    macRestoreFrontApp();
#endif
}

void SessionController::onTargetReset(quint64 taskId) {
    if (taskId != m_activeTaskId || m_state != PopupState::Translating) {
        return;
    }
}

void SessionController::onTargetAppended(quint64 taskId, const QString& piece) {
    if (taskId != m_activeTaskId || m_state != PopupState::Translating) {
        return;
    }

    m_popup->appendChunk(piece);
}

void SessionController::onTranslationFinished(quint64 taskId, int state) {
    if (taskId != m_activeTaskId) {
        return;
    }

    if (state == static_cast<int>(TaskState::Completed)) {
        m_popup->finishStreaming();
        m_state = PopupState::Showing;
    } else if (state == static_cast<int>(TaskState::Cancelled)) {
        m_popup->hide();
        resetSession();
    } else {
        m_popup->showError(QStringLiteral("Translation failed"));
        m_state = PopupState::Showing;
    }
}

void SessionController::onPopupDismissed() {
    resetSession();
}

bool SessionController::checkDebounce() {
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastTrigger < std::chrono::milliseconds(m_debounceMs)) {
        return false;
    }
    m_lastTrigger = now;
    return true;
}

void SessionController::resetSession() {
    m_state = PopupState::Idle;
    m_activeTaskId = 0;
}
