#include "ui/popup/session_controller.h"

#include "shared/string_bridge.h"
#include "app/inference_service.h"
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
    InferenceService *inferenceService,
    PopupWindow *popup,
    QObject *parent)
    : QObject(parent), m_hotkeyManager(hotkeyMgr), m_inferenceService(inferenceService), m_popup(popup) {
    connect(m_hotkeyManager, &HotkeyManager::hotkeyTriggered,
            this, &SessionController::onHotkeyTriggered);

    connect(m_inferenceService, &InferenceService::translationStarted,
            this, &SessionController::onTranslationStarted);
    connect(m_inferenceService, &InferenceService::translationDelta,
            this, &SessionController::onTranslationDelta);
    connect(m_inferenceService, &InferenceService::translationFinished,
            this, &SessionController::onTranslationFinished);

    connect(m_popup, &PopupWindow::dismissed,
            this, &SessionController::onPopupDismissed);
    connect(m_popup, &PopupWindow::retryRequested,
            this, &SessionController::onRetryRequested);
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

bool SessionController::setHotkey(const QString &shortcut) {
    const QString new_shortcut = shortcut.trimmed();
    const QKeySequence new_sequence(new_shortcut);
    if (new_sequence.isEmpty()) {
        m_hotkeyManager->unregisterHotkey(m_translateHotkeyId);
        m_hotkeyStr.clear();
        return true;
    }

    // Transactional replacement: the requested shortcut that is already the
    // active registration is a safe no-op — re-registering it would drop and
    // re-acquire the same binding for no benefit (and could fail, losing a
    // working shortcut). If the manager lost the binding while the session
    // still tracks it (startup failure, unregisterAll, ...), fall through to
    // an actual registration attempt so the runtime recovers.
    if (new_shortcut == m_hotkeyStr &&
        m_hotkeyManager->isRegistered(m_translateHotkeyId)) {
        return true;
    }

    const QString previous = m_hotkeyStr;
    const Qt::KeyboardModifiers mods = new_sequence[0].keyboardModifiers();
    const Qt::Key key = static_cast<Qt::Key>(new_sequence[0].key());

    // Register the new binding first. The manager replaces the binding for
    // the same id, so if registration fails the old shortcut must be
    // re-registered: a failed change must never leave the user without any
    // working shortcut.
    if (!m_hotkeyManager->registerHotkey(m_translateHotkeyId, mods, key)) {
        wordselect_logger()->warn(
            "failed to register hotkey '{}' (mods={} key={})",
            qtrans::app::to_utf8(new_shortcut),
            static_cast<int>(mods),
            static_cast<int>(key));
        if (!previous.isEmpty() && previous != new_shortcut) {
            const QKeySequence previous_sequence(previous);
            if (!m_hotkeyManager->registerHotkey(
                    m_translateHotkeyId,
                    previous_sequence[0].keyboardModifiers(),
                    static_cast<Qt::Key>(previous_sequence[0].key()))) {
                wordselect_logger()->error(
                    "rollback: re-registering previous hotkey '{}' failed",
                    qtrans::app::to_utf8(previous));
                // Neither binding is active: report no shortcut rather than
                // a stale one, so callers never persist a dead value.
                m_hotkeyStr.clear();
                return false;
            }
            wordselect_logger()->debug(
                "rollback: restored previous hotkey '{}'",
                qtrans::app::to_utf8(previous));
            // m_hotkeyStr already holds the now-active previous binding.
            return false;
        }
        // Nothing to roll back to (the previous registration was the same
        // shortcut or already gone): report the actual active state, which
        // is "no shortcut".
        m_hotkeyStr.clear();
        return false;
    }

    m_hotkeyStr = new_shortcut;
    return true;
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

    if (m_state == PopupState::Translating) {
        wordselect_logger()->debug("cancelling current translation");
        // Invalidate before the cancel/reset so nothing from the session
        // being replaced can re-enter afterwards.
        invalidateSession();
        cancelActiveJob();
        resetSession();
    } else if (m_state == PopupState::CaptureScheduled ||
               m_state == PopupState::Capturing) {
        // A newer hotkey supersedes a pending or in-flight capture: reset
        // the session (which bumps the token) so the stale capture's timer
        // callback rejects itself, then schedule a fresh capture below.
        wordselect_logger()->debug("new hotkey supersedes pending capture");
        resetSession();
    }

    if (!m_inferenceService->isModelLoaded()) {
        wordselect_logger()->warn("model not loaded");
        m_popup->showError(QStringLiteral("Model not loaded. Open main window and load a model first."));
#ifdef Q_OS_MACOS
        macRestoreFrontApp();
#endif
        return;
    }

    // Begin a fresh capture session. The token bump is what lets the
    // scheduled timer callback (and any capture already pumping the event
    // loop) tell this session apart from an older one.
    invalidateSession();
    m_state = PopupState::CaptureScheduled;
#ifdef Q_OS_MACOS
    macSaveFrontApp();
#endif
    QTimer::singleShot(50, this, &SessionController::doTranslate);
}

void SessionController::doTranslate() {
    // Timer callback for the capture scheduled by the last hotkey. Snapshot
    // the session token now: clipboard capture pumps the Qt event loop, so
    // the session may be dismissed or superseded while we are inside it, and
    // a stale capture must never submit a translation.
    const std::uint64_t token = m_sessionToken;
    if (m_state != PopupState::CaptureScheduled) {
        wordselect_logger()->debug(
            "doTranslate: no capture scheduled (state={}), skipping",
            static_cast<int>(m_state));
        return;
    }

    m_state = PopupState::Capturing;

    wordselect_logger()->debug("capturing clipboard text");
#ifdef Q_OS_MACOS
    if (!macEnsureAccessibilityTrusted(true)) {
        wordselect_logger()->warn("accessibility permission not granted");
        m_popup->showError(QStringLiteral(
            "Accessibility permission is required to copy selected text. "
            "Enable QTrans in System Settings \u2192 Privacy & Security \u2192 Accessibility, then try again."));
        resetSession();
        return;
    }

    macRestoreFrontApp();
    QThread::msleep(120);
#endif

    const QString text = ClipboardCapture::captureSelectedText(500);
    // The capture pumped the Qt event loop: the session may have been reset
    // (popup dismissed) or superseded (newer hotkey) meanwhile. Reject the
    // stale capture by state and token before submitting anything.
    if (m_state != PopupState::Capturing || token != m_sessionToken) {
        wordselect_logger()->debug(
            "doTranslate: stale capture (state={}, token {} != {}), discarding",
            static_cast<int>(m_state),
            token,
            m_sessionToken);
        return;
    }

    if (text.isEmpty()) {
        wordselect_logger()->warn("captured text is empty, resetting session");
        m_popup->showError(QStringLiteral(
            "Could not copy selected text. Select text in the front app first, "
            "then press the shortcut again."));
        resetSession();
        return;
    }

    wordselect_logger()->trace(
        "captured text: '{}' (len={})",
        qtrans::app::to_utf8(text),
        static_cast<int>(text.size()));

    NativeTranslationRequest request;
    request.source = qtrans::app::to_utf8(text);
    request.target_language = qtrans::app::to_utf8(m_targetLanguage);
    request.source_language = qtrans::app::to_utf8(m_sourceLanguage);
    request.back_translate = false;
    request.wordselect = true;
    submitRequest(request, text);
}

void SessionController::submitRequest(const NativeTranslationRequest &request,
                                      const QString &sourceText) {
    // A new request replaces the retained one: invalidate the session token
    // first so any pending capture timer or job signal from an earlier
    // session is rejected from here on.
    invalidateSession();
    m_jobToken = m_sessionToken;
    m_state = PopupState::Translating;
    m_activeJobId = TranslationJobId{};

    // Retain the request so a failure can offer Retry with the exact same
    // source and languages.
    m_lastRequest = request;
    m_lastSourceText = sourceText;
    m_hasLastRequest = true;

    m_activeJobId = m_inferenceService->translateNative(m_lastRequest);
}

void SessionController::onRetryRequested() {
    wordselect_logger()->debug("retry requested, state={}", static_cast<int>(m_state));
    if (m_state == PopupState::Translating ||
        m_state == PopupState::CaptureScheduled ||
        m_state == PopupState::Capturing) {
        // A job is already in flight (or a capture is pending that will
        // submit one); a second retry click must not submit a duplicate
        // that would orphan the active job id.
        wordselect_logger()->debug("retry ignored: session busy");
        return;
    }
    if (!m_hasLastRequest) {
        m_popup->showError(QStringLiteral("Nothing to retry."));
        return;
    }
    if (!m_inferenceService->isModelLoaded()) {
        m_popup->showError(QStringLiteral(
            "Model not loaded. Open main window and load a model first."));
        return;
    }

    // The popup keeps showing; the retried job's start event swaps it to
    // the loading state with the retained source.
    submitRequest(m_lastRequest, m_lastSourceText);
}

void SessionController::onTranslationStarted(TranslationJobId jobId) {
    if (m_state != PopupState::Translating ||
        m_jobToken != m_sessionToken ||
        jobId != m_activeJobId) {
        return;
    }
    // The active job id is the synchronously returned value from
    // translateNative(); ignore started events from unrelated jobs.
    m_popup->showLoading(m_lastSourceText);
#ifdef Q_OS_MACOS
    macRestoreFrontApp();
#endif
}

void SessionController::onTranslationDelta(TranslationJobId jobId,
                                           TranslationChannel channel,
                                           const QString &piece) {
    if (m_state != PopupState::Translating ||
        m_jobToken != m_sessionToken ||
        jobId != m_activeJobId) {
        return;
    }
    if (channel != TranslationChannel::Target) {
        return;
    }

    m_popup->appendChunk(piece);
}

void SessionController::onTranslationFinished(const TranslationJobResult &result) {
    if (m_jobToken != m_sessionToken || result.id != m_activeJobId) {
        return;
    }

    if (result.state == TranslationState::Completed) {
        m_popup->finishStreaming();
        m_state = PopupState::Showing;
    } else if (result.state == TranslationState::Cancelled ||
               result.state == TranslationState::Preempted) {
        // Reset before hiding: hide() emits dismissed() synchronously, and
        // the dismissal handler must see an idle session so it never
        // re-cancels a job that already reached its terminal state.
        resetSession();
        m_popup->hide();
    } else {
        QString message = QString::fromStdString(result.error_message).trimmed();
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

void SessionController::onPopupDismissed() {
    // While a capture is scheduled or in flight, a dismissal belongs to the
    // prior popup presentation being auto-closed during the clipboard
    // capture event pump, not to the capture session. Ignore it: the new
    // capture must not be invalidated, reset, or cancelled, or its
    // translation would never surface — the capture flow re-shows the popup
    // once the translation starts.
    if (m_state == PopupState::CaptureScheduled ||
        m_state == PopupState::Capturing) {
        wordselect_logger()->debug(
            "popup dismissed during capture (state={}), ignoring",
            static_cast<int>(m_state));
        return;
    }

    // Invalidate the session before anything else: a capture may be in
    // flight (clipboard capture pumps the Qt event loop), and once the pump
    // unwinds the stale capture must not submit. The invalidation also keeps
    // job signals from the cancelled work out of any newer session.
    invalidateSession();

    // A visible popup dismissed mid-stream must stop its in-flight job
    // before session state resets. The state + id checks below are what
    // keep this from ever cancelling a stale/finished id: resetSession()
    // (or the terminal-state handler) clears the active id first, and a
    // finished job only ever leaves the Translating state here.
    cancelActiveJob();
#ifdef Q_OS_MACOS
    // The popup never activates, so normally nothing to restore; this is a
    // safety net for dismissal before the started event ran the restore
    // (e.g. Escape pressed while the 50 ms capture delay is pending).
    macRestoreFrontApp();
#endif
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

std::uint64_t SessionController::invalidateSession() {
    return ++m_sessionToken;
}

bool SessionController::cancelActiveJob() {
    if (m_state != PopupState::Translating || !m_activeJobId.is_valid()) {
        return false;
    }
    wordselect_logger()->debug(
        "cancelling job:{}", m_activeJobId.value);
    m_inferenceService->cancel(m_activeJobId);
    return true;
}

void SessionController::resetSession() {
    // Bump the token first: any timer callback or job signal still carrying
    // this session's generation must reject itself from here on.
    invalidateSession();
    m_state = PopupState::Idle;
    m_activeJobId = TranslationJobId{};
    m_lastRequest = NativeTranslationRequest{};
    m_lastSourceText.clear();
    m_hasLastRequest = false;
}
