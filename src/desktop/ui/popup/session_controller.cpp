#include "ui/popup/session_controller.h"

#include "shared/string_bridge.h"
#include "app/task_service.h"
#include "domain/logging/component.h"
#include "domain/logging/logger.h"
#include "domain/platform/clipboard/clipboard_capture.h"
#include "domain/platform/hotkeys/hotkey_manager.h"
#include "ui/popup/popup_window.h"

#include <QMetaObject>
#include <QThread>
#include <QTimer>

#ifdef Q_OS_MACOS
#include "domain/platform/mac/platform_utils.h"
#endif

namespace {

auto wordselect_logger() {
    return qtrans::log::get(qtrans::log::Component::WordSelect);
}

}  // namespace

SessionController::SessionController(
    HotkeyManager *hotkeyMgr,
    TaskService *taskService,
    PopupWindow *popup,
    QObject *parent)
    : QObject(parent), m_hotkeyManager(hotkeyMgr), m_taskService(taskService), m_popup(popup) {
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
    connect(m_taskService, &TaskService::statusChanged,
            this, &SessionController::onStatusChanged);

    connect(m_popup, &PopupWindow::dismissed,
            this, &SessionController::onPopupDismissed);
}

void SessionController::initialize() {
    m_hotkeyStr = QStringLiteral("Ctrl+`");
    setHotkey(m_hotkeyStr);
    m_sourceLanguage = QStringLiteral("Auto");
    m_targetLanguage = QStringLiteral("Chinese");
}

void SessionController::setTranslateLanguages(const QString &source, const QString &target) {
    m_sourceLanguage = source;
    m_targetLanguage = target;
}

void SessionController::setHotkey(const QString &shortcut) {
    m_hotkeyStr = shortcut.trimmed();
    const QKeySequence ks(m_hotkeyStr);
    if (ks.isEmpty()) {
        m_hotkeyManager->unregisterHotkey(m_translateHotkeyId);
        return;
    }

    const Qt::KeyboardModifiers mods = ks[0].keyboardModifiers();
    const Qt::Key key = static_cast<Qt::Key>(ks[0].key());

    m_hotkeyManager->unregisterHotkey(m_translateHotkeyId);
    if (!m_hotkeyManager->registerHotkey(m_translateHotkeyId, mods, key)) {
        wordselect_logger()->warn(
            "failed to register hotkey '{}' (mods={} key={})",
            qtrans::app::to_utf8(m_hotkeyStr),
            static_cast<int>(mods),
            static_cast<int>(key));
    }
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
    if (hotkeyId != m_translateHotkeyId) {
        return;
    }

    if (!m_enabled) {
        wordselect_logger()->debug("hotkey ignored: word select disabled");
        return;
    }

    if (!checkDebounce()) {
        return;
    }

    wordselect_logger()->debug("hotkey triggered, state={}", static_cast<int>(m_state));

    if (m_state == PopupState::Capturing) {
        wordselect_logger()->debug("already capturing, ignoring");
        return;
    }

    if (m_state == PopupState::Translating) {
        wordselect_logger()->debug("cancelling current translation");
        if (m_activeTaskId != 0) {
            TaskId id{};
            id.value = m_activeTaskId;
            m_taskService->cancel(id);
        }
        resetSession();
    }

    if (!m_taskService->isModelLoaded()) {
        wordselect_logger()->warn("model not loaded");
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
        wordselect_logger()->debug(
            "doTranslate: not capturing (state={}), skipping",
            static_cast<int>(m_state));
        return;
    }

    wordselect_logger()->debug("capturing clipboard text");
#ifdef Q_OS_MACOS
    if (!macEnsureAccessibilityTrusted(true)) {
        wordselect_logger()->warn("accessibility permission not granted");
        m_popup->showError(QStringLiteral(
            "Accessibility permission is required to copy selected text. "
            "Enable QTrans in System Settings \u2192 Privacy & Security \u2192 Accessibility, then try again."));
        m_state = PopupState::Showing;
        resetSession();
        return;
    }

    macRestoreFrontApp();
    QThread::msleep(120);
#endif

    const QString text = ClipboardCapture::captureSelectedText(500);
    if (text.isEmpty()) {
        wordselect_logger()->warn("captured text is empty, resetting session");
        m_popup->showError(QStringLiteral(
            "Could not copy selected text. Select text in the front app first, "
            "then press the shortcut again."));
        m_state = PopupState::Showing;
        resetSession();
        return;
    }

    wordselect_logger()->trace(
        "captured text: '{}' (len={})",
        qtrans::app::to_utf8(text),
        static_cast<int>(text.size()));

    m_state = PopupState::Translating;
    m_activeTaskId = 0;
    m_lastErrorMessage.clear();

    QMetaObject::invokeMethod(
        m_taskService,
        "translateInteractive",
        Qt::QueuedConnection,
        Q_ARG(QString, text),
        Q_ARG(QString, m_targetLanguage),
        Q_ARG(QString, m_sourceLanguage),
        Q_ARG(bool, false),
        Q_ARG(bool, true));
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

void SessionController::onTargetAppended(quint64 taskId, const QString &piece) {
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
        QString message = m_lastErrorMessage.trimmed();
        if (message.startsWith(QStringLiteral("Error:"))) {
            message = message.mid(6).trimmed();
        }
        if (message.isEmpty()) {
            message = QStringLiteral("Translation failed");
        }
        m_popup->showError(message);
        m_state = PopupState::Showing;
    }
}

void SessionController::onStatusChanged(const QString &message, bool busy) {
    if (m_state != PopupState::Translating || busy) {
        return;
    }
    if (message.startsWith(QStringLiteral("Error:"))) {
        m_lastErrorMessage = message;
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
    m_lastErrorMessage.clear();
}
