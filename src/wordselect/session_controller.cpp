#include "wordselect/session_controller.h"

#include "app/task_service.h"
#include "wordselect/clipboard_capture.h"
#include "wordselect/hotkey_manager.h"
#include "wordselect/popup_window.h"

#include <QMetaObject>
#include <QTimer>

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
    m_sourceLanguage = QStringLiteral("English");
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

    if (m_state == PopupState::Capturing) {
        return;
    }

    if (m_state == PopupState::Translating) {
        if (m_activeTaskId != 0) {
            TaskId id{};
            id.value = m_activeTaskId;
            m_taskService->cancel(id);
        }
        resetSession();
    }

    if (!m_taskService->isModelLoaded()) {
        m_popup->showError(QString::fromUtf8("\346\250\241\345\236\213\346\234\252\345\212\240\350\275\275\357\274\214\350\257\267\345\205\210\346\211\223\345\274\200\344\270\273\347\252\227\345\217\243\345\212\240\350\275\275\346\250\241\345\236\213"));
        return;
    }

    m_state = PopupState::Capturing;
    QTimer::singleShot(50, this, &SessionController::doTranslate);
}

void SessionController::doTranslate() {
    if (m_state != PopupState::Capturing) {
        return;
    }

    const QString text = ClipboardCapture::captureSelectedText(300);
    if (text.isEmpty()) {
        resetSession();
        return;
    }

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
        m_popup->showError(QString::fromUtf8("\347\277\273\350\257\221\345\244\261\350\264\245")); // 翻译失败
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
