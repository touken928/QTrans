#include "ui/popup/popup_window.h"
#include "ui/shared/theme/app_theme.h"
#include "ui/shared/theme/theme.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QShowEvent>
#include <QStyle>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef Q_OS_MACOS
#include "domain/platform/mac/platform_utils.h"
#include "domain/platform/mac/popup_platform.h"

#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#endif

namespace {

void repolish(QWidget *widget) {
    if (widget != nullptr && widget->style() != nullptr) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
    }
}

// ── Escape dismissal listeners ────────────────────────────────────────
// The popup is non-activating (WA_ShowWithoutActivating + ToolTip window
// type), so keyboard input normally stays with the front app and Qt key
// events never reach the popup. While the popup is visible we therefore
// install a narrowly scoped set of listeners, all removed in hideEvent:
//  • a Qt application event filter (every platform) — catches Escape when
//    any QTrans surface has key focus;
//  • on macOS, a global CGEventTap — gated on the accessibility trust that
//    word-select already demands; while active, Escape anywhere is
//    swallowed and dismisses the popup;
//  • on Windows, a low-level WH_KEYBOARD_LL hook — needs no special
//    privileges; while active, Escape anywhere is swallowed and dismisses
//    the popup.
// The popup's own keyPressEvent remains a last-resort fallback. A modal
// dialog in the main window keeps its Escape (the listeners pass it
// through), and the front app is never activated: dismissal only hides the
// popup, and the session restores the source app on macOS.

// True when our app has a modal surface that should keep its Escape.
bool modalConsumesEscape() {
    return QApplication::activeModalWidget() != nullptr;
}

#ifdef Q_OS_MACOS

CFMachPortRef g_escape_tap_port = nullptr;
CFRunLoopSourceRef g_escape_tap_source = nullptr;
PopupWindow *g_escape_tap_owner = nullptr;

CGEventRef escapeTapCallback(CGEventTapProxy proxy, CGEventType type,
                             CGEventRef event, void *info) {
    Q_UNUSED(proxy);
    Q_UNUSED(info);
    if (type == kCGEventTapDisabledByTimeout ||
        type == kCGEventTapDisabledByUserInput) {
        // Re-enable as long as the popup still owns the tap.
        if (g_escape_tap_port != nullptr && g_escape_tap_owner != nullptr) {
            CGEventTapEnable(g_escape_tap_port, true);
        }
        return event;
    }
    if (type == kCGEventKeyDown && !modalConsumesEscape() &&
        CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode) == kVK_Escape) {
        if (PopupWindow *owner = g_escape_tap_owner) {
            // Deferred: never mutate Qt state from inside the tap callback.
            QMetaObject::invokeMethod(owner, [owner]() { owner->dismissByEscape(); }, Qt::QueuedConnection);
        }
        // Swallow: while the popup is visible, Escape belongs to it.
        return nullptr;
    }
    return event;
}

#endif  // Q_OS_MACOS

#ifdef Q_OS_WIN

HHOOK g_escape_hook = nullptr;
PopupWindow *g_escape_hook_owner = nullptr;

LRESULT CALLBACK escapeHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN && !modalConsumesEscape()) {
        const auto *kb = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
        if (kb->vkCode == VK_ESCAPE) {
            if (PopupWindow *owner = g_escape_hook_owner) {
                // Deferred: never mutate Qt state from inside the hook.
                QMetaObject::invokeMethod(owner, [owner]() { owner->dismissByEscape(); }, Qt::QueuedConnection);
            }
            // Swallow: while the popup is visible, Escape belongs to it.
            return 1;
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

#endif  // Q_OS_WIN

}  // namespace

PopupWindow::PopupWindow(QWidget *parent)
    : QWidget(parent) {
    setWindowFlags(
        Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::ToolTip);
    // The popup must never steal focus from the front app: it stays
    // non-activating and keeps the ToolTip window type (which macOS and
    // Windows both treat as a transient, non-focus-stealing surface).
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    // Transparent backing so the popupFrame's rounded corners actually show:
    // without it the opaque window paints a rectangle over the frame's radius.
    setAttribute(Qt::WA_TranslucentBackground, true);

#ifdef Q_OS_MACOS
    // Create the native window up-front so the popup's NSWindow exists before
    // the first show and the non-activating/active-Space configuration can be
    // applied ahead of time (Qt creates it lazily on first show otherwise).
    createWinId();
    macConfigurePopupWindow(reinterpret_cast<void *>(winId()));
#endif

    setMinimumWidth(MIN_WIDTH);
    setMaximumWidth(MAX_WIDTH);
    setMaximumHeight(MAX_HEIGHT);

    setupUI();

    m_closeTimer = new QTimer(this);
    m_closeTimer->setSingleShot(true);
    connect(m_closeTimer, &QTimer::timeout, this, &QWidget::hide);
}

void PopupWindow::setupUI() {
    m_frame = new QFrame(this);
    m_frame->setObjectName(QStringLiteral("popupFrame"));

    auto *layout = new QVBoxLayout(m_frame);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(8);

    // ── Header: brand glyph + title + pin + close ───────────────────
    // The header bar is its own widget so its hairline bottom border
    // separates the chrome row from the content sections.
    m_headerBar = new QWidget(m_frame);
    m_headerBar->setObjectName(QStringLiteral("popupHeader"));
    auto *headerRow = new QHBoxLayout(m_headerBar);
    headerRow->setContentsMargins(0, 0, 0, 6);
    headerRow->setSpacing(6);

    auto *titleGlyph = new QLabel(QString::fromUtf8(Theme::NavIcon::translate), m_headerBar);
    titleGlyph->setObjectName(QStringLiteral("popupTitleGlyph"));
    titleGlyph->setAccessibleName(QStringLiteral("Translate"));
    headerRow->addWidget(titleGlyph);

    auto *titleLabel = new QLabel(QStringLiteral("Translation"), m_headerBar);
    titleLabel->setObjectName(QStringLiteral("popupTitle"));
    headerRow->addWidget(titleLabel);
    headerRow->addStretch(1);

    m_pinBtn = new QPushButton(QStringLiteral("Pin"), m_headerBar);
    m_pinBtn->setObjectName(QStringLiteral("popupPinBtn"));
    m_pinBtn->setCheckable(true);
    m_pinBtn->setCursor(Qt::PointingHandCursor);
    m_pinBtn->setToolTip(QStringLiteral("Keep the popup open"));
    m_pinBtn->setAccessibleName(QStringLiteral("Pin popup"));
    headerRow->addWidget(m_pinBtn);

    m_closeBtn = new QPushButton(QStringLiteral("\u2715"), m_headerBar);
    m_closeBtn->setObjectName(QStringLiteral("popupCloseBtn"));
    m_closeBtn->setFixedSize(22, 22);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setToolTip(QStringLiteral("Close (Esc)"));
    m_closeBtn->setAccessibleName(QStringLiteral("Close popup"));
    headerRow->addWidget(m_closeBtn);

    layout->addWidget(m_headerBar);

    // ── Bounded source preview ───────────────────────────────────────
    auto *sourceBox = new QFrame(m_frame);
    sourceBox->setObjectName(QStringLiteral("popupSourceBox"));
    auto *sourceLayout = new QVBoxLayout(sourceBox);
    sourceLayout->setContentsMargins(8, 6, 8, 6);
    sourceLayout->setSpacing(2);

    auto *sourceCaption = new QLabel(QStringLiteral("SOURCE"), sourceBox);
    sourceCaption->setObjectName(QStringLiteral("popupSectionLabel"));
    sourceLayout->addWidget(sourceCaption);

    m_sourceLabel = new QLabel(sourceBox);
    m_sourceLabel->setObjectName(QStringLiteral("popupSource"));
    m_sourceLabel->setWordWrap(true);
    m_sourceLabel->setTextFormat(Qt::PlainText);
    m_sourceLabel->setMaximumHeight(56);  // ~3 lines at the compact size
    m_sourceLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sourceLayout->addWidget(m_sourceLabel);

    layout->addWidget(sourceBox);
    m_sourceBox = sourceBox;

    // ── Scrollable translation output ─────────────────────────────────
    // The result header is its own widget so the whole section (caption +
    // Copy) collapses together when an error leaves no partial result.
    m_resultHeader = new QWidget(m_frame);
    m_resultHeader->setObjectName(QStringLiteral("popupResultHeader"));
    auto *resultHeader = new QHBoxLayout(m_resultHeader);
    resultHeader->setContentsMargins(0, 0, 0, 0);
    resultHeader->setSpacing(4);

    auto *resultCaption = new QLabel(QStringLiteral("RESULT"), m_resultHeader);
    resultCaption->setObjectName(QStringLiteral("popupSectionLabel"));
    resultHeader->addWidget(resultCaption);
    resultHeader->addStretch(1);

    m_copyBtn = new QPushButton(QStringLiteral("Copy"), m_resultHeader);
    m_copyBtn->setObjectName(QStringLiteral("popupCopyBtn"));
    m_copyBtn->setCursor(Qt::PointingHandCursor);
    m_copyBtn->setToolTip(QStringLiteral("Copy the translation"));
    m_copyBtn->setAccessibleName(QStringLiteral("Copy translation"));
    m_copyBtn->setVisible(false);
    resultHeader->addWidget(m_copyBtn);

    layout->addWidget(m_resultHeader);

    m_resultEdit = new QPlainTextEdit(m_frame);
    m_resultEdit->setObjectName(QStringLiteral("popupResult"));
    m_resultEdit->setReadOnly(true);
    m_resultEdit->setTabChangesFocus(true);
    m_resultEdit->setMaximumHeight(168);  // ~7 lines, scrolls beyond
    m_resultEdit->setPlaceholderText(QStringLiteral("Translation appears here\u2026"));
    layout->addWidget(m_resultEdit);

    // ── Status footer: stream indicator + dot + text ─────────────────
    // A hairline top border anchors the footer under the content; the
    // indeterminate progress bar fills the row while text streams in.
    m_statusRow = new QWidget(m_frame);
    m_statusRow->setObjectName(QStringLiteral("popupStatusRow"));
    auto *statusLayout = new QHBoxLayout(m_statusRow);
    statusLayout->setContentsMargins(0, 6, 0, 0);
    statusLayout->setSpacing(6);

    m_progressBar = new QProgressBar(m_statusRow);
    m_progressBar->setObjectName(QStringLiteral("popupProgress"));
    m_progressBar->setRange(0, 0);  // indeterminate while streaming
    m_progressBar->setTextVisible(false);
    statusLayout->addWidget(m_progressBar, 1);

    m_statusDot = new QLabel(m_statusRow);
    m_statusDot->setObjectName(QStringLiteral("popupStatusDot"));
    m_statusDot->setFixedSize(Theme::Size::statusDot, Theme::Size::statusDot);
    statusLayout->addWidget(m_statusDot);

    m_statusLabel = new QLabel(m_statusRow);
    m_statusLabel->setObjectName(QStringLiteral("popupStatus"));
    statusLayout->addWidget(m_statusLabel);

    layout->addWidget(m_statusRow);

    // ── Error banner: alert chip + message + retry ───────────────────
    // Replaces the status footer while a job fails; any partial result
    // stays visible above it.
    m_errorBox = new QFrame(m_frame);
    m_errorBox->setObjectName(QStringLiteral("popupErrorBox"));
    auto *errorLayout = new QVBoxLayout(m_errorBox);
    errorLayout->setContentsMargins(10, 8, 10, 10);
    errorLayout->setSpacing(8);

    auto *errorRow = new QHBoxLayout();
    errorRow->setContentsMargins(0, 0, 0, 0);
    errorRow->setSpacing(8);

    m_errorIcon = new QLabel(QStringLiteral("!"), m_errorBox);
    m_errorIcon->setObjectName(QStringLiteral("popupErrorIcon"));
    m_errorIcon->setFixedSize(16, 16);
    m_errorIcon->setAlignment(Qt::AlignCenter);
    errorRow->addWidget(m_errorIcon);

    m_errorLabel = new QLabel(m_errorBox);
    m_errorLabel->setObjectName(QStringLiteral("popupErrorText"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setTextFormat(Qt::PlainText);
    m_errorLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_errorLabel->setMaximumHeight(84);  // ~4 wrapped lines
    m_errorLabel->setAccessibleName(QStringLiteral("Error message"));
    errorRow->addWidget(m_errorLabel, 1);
    errorLayout->addLayout(errorRow);

    auto *retryRow = new QHBoxLayout();
    retryRow->setContentsMargins(0, 0, 0, 0);
    retryRow->addStretch(1);

    m_retryBtn = new QPushButton(QStringLiteral("Retry"), m_errorBox);
    m_retryBtn->setObjectName(QStringLiteral("popupRetryBtn"));
    m_retryBtn->setCursor(Qt::PointingHandCursor);
    m_retryBtn->setToolTip(QStringLiteral("Retry the failed translation"));
    m_retryBtn->setAccessibleName(QStringLiteral("Retry translation"));
    retryRow->addWidget(m_retryBtn);
    errorLayout->addLayout(retryRow);

    m_errorBox->setVisible(false);
    layout->addWidget(m_errorBox);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(m_frame);

    // Apply shared theme stylesheet instead of hardcoded QSS
    AppTheme::applyPopup(this);

    connect(m_closeBtn, &QPushButton::clicked, this, &PopupWindow::onCloseClicked);
    connect(m_copyBtn, &QPushButton::clicked, this, &PopupWindow::onCopyClicked);
    connect(m_retryBtn, &QPushButton::clicked, this, &PopupWindow::onRetryClicked);
    connect(m_pinBtn, &QPushButton::toggled, this, &PopupWindow::onPinToggled);

    setStatusState(QStringLiteral("idle"));
}

void PopupWindow::showLoading(const QString &sourceText) {
    // A stale auto-close countdown from a previous presentation must never
    // hide the popup mid-stream: every new loading presentation starts from
    // a stopped timer.
    m_closeTimer->stop();
    m_isStreaming = true;

    // Bounded source preview: only present when a source was captured.
    m_sourceLabel->setText(sourceText);
    m_sourceBox->setVisible(!sourceText.trimmed().isEmpty());

    m_resultEdit->clear();
    m_resultEdit->setVisible(true);
    m_resultHeader->setVisible(true);
    m_errorBox->setVisible(false);
    m_retryBtn->setVisible(false);
    m_progressBar->setVisible(true);
    m_statusRow->setVisible(true);
    m_resultEdit->setPlaceholderText(QStringLiteral("Translation is streaming in\u2026"));
    resetCopyButton();
    updateCopyButton();
    setStatusState(QStringLiteral("translating"));
    m_statusLabel->setText(QStringLiteral("Translating\u2026"));

    positionNearCursor();
    show();
    raise();
}

void PopupWindow::appendChunk(const QString &chunk) {
    if (!m_isStreaming) return;

    m_resultEdit->moveCursor(QTextCursor::End);
    m_resultEdit->insertPlainText(chunk);
    m_resultEdit->moveCursor(QTextCursor::End);
    m_resultEdit->ensureCursorVisible();
    updateCopyButton();
}

void PopupWindow::finishStreaming() {
    if (!m_isStreaming) return;

    m_isStreaming = false;
    m_progressBar->setVisible(false);
    m_resultEdit->setPlaceholderText(QStringLiteral("Translation appears here\u2026"));
    setStatusState(QStringLiteral("done"));
    m_statusLabel->setText(QStringLiteral("Done"));
    updateCopyButton();
    startAutoClose();
}

void PopupWindow::showError(const QString &message) {
    // Same stale-timer rule as showLoading: an old countdown must not hide
    // the fresh error presentation early.
    m_closeTimer->stop();
    m_isStreaming = false;

    // The error banner replaces the status footer; the alert chip, wrapped
    // message and Retry carry the failure state by themselves.
    m_errorLabel->setText(message);
    m_errorBox->setVisible(true);
    m_retryBtn->setVisible(true);
    m_progressBar->setVisible(false);
    m_statusRow->setVisible(false);

    // Keep any partial result visible; without one, collapse the result
    // section so the error itself leads the popup.
    const bool hasResult = !m_resultEdit->toPlainText().isEmpty();
    m_resultEdit->setVisible(hasResult);
    m_resultHeader->setVisible(hasResult);
    m_sourceBox->setVisible(!m_sourceLabel->text().trimmed().isEmpty());

    resetCopyButton();
    updateCopyButton();
    setStatusState(QStringLiteral("error"));

    positionNearCursor();
    show();
    startAutoClose();
}

void PopupWindow::setAutoCloseMs(int ms) {
    m_autoCloseMs = ms;
}

int PopupWindow::autoCloseMs() const {
    return m_autoCloseMs;
}

bool PopupWindow::isStreaming() const {
    return m_isStreaming;
}

bool PopupWindow::isPinned() const {
    return m_pinned;
}

void PopupWindow::onCloseClicked() {
    m_closeTimer->stop();
    hide();
}

void PopupWindow::onCopyClicked() {
    QApplication::clipboard()->setText(m_resultEdit->toPlainText());
    // Brief confirmation on the button itself; every presentation and the
    // hide path reset the label, so a stale "Copied" never leaks across
    // sessions.
    m_copyBtn->setText(QStringLiteral("Copied"));
    QTimer::singleShot(1400, this, [this]() { resetCopyButton(); });
}

void PopupWindow::resetCopyButton() {
    m_copyBtn->setText(QStringLiteral("Copy"));
}

void PopupWindow::onRetryClicked() {
    // The session controller retains the request and resubmits it; the
    // popup itself just asks. The result area is cleared by showLoading()
    // when the retried job starts streaming.
    emit retryRequested();
}

void PopupWindow::onPinToggled(bool pinned) {
    m_pinned = pinned;
    if (pinned) {
        m_closeTimer->stop();
        return;
    }
    // Unpinning restarts the auto-dismiss only when nothing is running and
    // the pointer has already left the popup (leaveEvent manages that).
    if (!m_isStreaming && !underMouse()) {
        startAutoClose();
    }
}

void PopupWindow::dismissByEscape() {
    if (!isVisible()) {
        return;
    }
    onCloseClicked();
}

bool PopupWindow::eventFilter(QObject *watched, QEvent *event) {
    Q_UNUSED(watched);
    // Narrowly scoped: this filter is installed only while the popup is
    // visible (showEvent/hideEvent). While visible, Escape dismisses the
    // popup even when the popup itself does not hold key focus — but a
    // modal dialog in the main window keeps its Escape.
    if (event->type() == QEvent::KeyPress && !modalConsumesEscape()) {
        auto *key_event = static_cast<QKeyEvent *>(event);
        if (key_event->key() == Qt::Key_Escape) {
            // Defer: dismissing here would remove this filter from qApp
            // while Qt is iterating the filter chain.
            QMetaObject::invokeMethod(this, [this]() { dismissByEscape(); }, Qt::QueuedConnection);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PopupWindow::showEvent(QShowEvent *event) {
#ifdef Q_OS_MACOS
    // Re-apply the window configuration on every show, and keep the user's
    // front app active across a short window: presenting the popup can
    // activate QTrans at deferred points (notably when QTrans is minimized to
    // the Dock), which would otherwise pull the Space over under Stage Manager.
    macConfigurePopupWindow(reinterpret_cast<void *>(winId()));
    for (int delay : {0, 60, 150, 300, 600}) {
        QTimer::singleShot(delay, this, []() { macReassertFrontApp(); });
    }
#endif
    installEscapeMonitors();
    QWidget::showEvent(event);
}

void PopupWindow::hideEvent(QHideEvent *event) {
    m_isStreaming = false;
    m_closeTimer->stop();
    resetCopyButton();
    uninstallEscapeMonitors();
    emit dismissed();
    QWidget::hideEvent(event);
}

void PopupWindow::installEscapeMonitors() {
    if (m_escapeMonitorsInstalled) {
        return;
    }
    m_escapeMonitorsInstalled = true;
    QApplication::instance()->installEventFilter(this);

#ifdef Q_OS_MACOS
    // The global tap needs the accessibility trust word-select already
    // requires; without it we fall back to the Qt-level filter only.
    if (AXIsProcessTrusted() && g_escape_tap_port == nullptr) {
        g_escape_tap_port = CGEventTapCreate(
            kCGHIDEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
            CGEventMaskBit(kCGEventKeyDown), &escapeTapCallback, nullptr);
        if (g_escape_tap_port != nullptr) {
            g_escape_tap_source = CFMachPortCreateRunLoopSource(
                kCFAllocatorDefault, g_escape_tap_port, 0);
            if (g_escape_tap_source != nullptr) {
                CFRunLoopAddSource(CFRunLoopGetMain(), g_escape_tap_source,
                                   kCFRunLoopCommonModes);
            }
            CGEventTapEnable(g_escape_tap_port, true);
            g_escape_tap_owner = this;
        }
    }
#endif  // Q_OS_MACOS

#ifdef Q_OS_WIN
    if (g_escape_hook == nullptr) {
        g_escape_hook = SetWindowsHookExW(WH_KEYBOARD_LL, &escapeHookProc,
                                          GetModuleHandleW(nullptr), 0);
        if (g_escape_hook != nullptr) {
            g_escape_hook_owner = this;
        }
    }
#endif  // Q_OS_WIN
}

void PopupWindow::uninstallEscapeMonitors() {
    if (!m_escapeMonitorsInstalled) {
        return;
    }
    m_escapeMonitorsInstalled = false;
    QApplication::instance()->removeEventFilter(this);

#ifdef Q_OS_MACOS
    if (g_escape_tap_owner == this) {
        g_escape_tap_owner = nullptr;
        if (g_escape_tap_source != nullptr) {
            CFRunLoopRemoveSource(CFRunLoopGetMain(), g_escape_tap_source,
                                  kCFRunLoopCommonModes);
            CFRelease(g_escape_tap_source);
            g_escape_tap_source = nullptr;
        }
        if (g_escape_tap_port != nullptr) {
            CGEventTapEnable(g_escape_tap_port, false);
            CFRelease(g_escape_tap_port);
            g_escape_tap_port = nullptr;
        }
    }
#endif  // Q_OS_MACOS

#ifdef Q_OS_WIN
    if (g_escape_hook_owner == this) {
        g_escape_hook_owner = nullptr;
        if (g_escape_hook != nullptr) {
            UnhookWindowsHookEx(g_escape_hook);
            g_escape_hook = nullptr;
        }
    }
#endif  // Q_OS_WIN
}

void PopupWindow::keyPressEvent(QKeyEvent *event) {
    // Last-resort fallback: normally the application event filter consumes
    // Escape before delivery while the popup is visible.
    if (event->key() == Qt::Key_Escape) {
        dismissByEscape();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void PopupWindow::enterEvent(QEnterEvent *event) {
    m_closeTimer->stop();
    QWidget::enterEvent(event);
}

void PopupWindow::leaveEvent(QEvent *event) {
    if (!m_isStreaming && !m_pinned) {
        startAutoClose();
    }
    QWidget::leaveEvent(event);
}

void PopupWindow::positionNearCursor() {
    const QPoint cursorPos = QCursor::pos();
    QScreen *screen = QApplication::screenAt(cursorPos);
    if (!screen) {
        screen = QApplication::primaryScreen();
    }
    if (!screen) return;

    const QRect geom = screen->availableGeometry();
    // Responsive constrained sizing: the popup never exceeds the screen's
    // usable area, even on small displays — the result pane then scrolls
    // instead of overflowing the edges.
    const int usableW = geom.width() - 2 * EDGE_MARGIN;
    const int usableH = geom.height() - 2 * EDGE_MARGIN;
    setMaximumWidth(qMax(MIN_WIDTH, qMin(MAX_WIDTH, usableW)));
    setMaximumHeight(qMax(160, qMin(MAX_HEIGHT, usableH)));
    adjustSize();

    const int w = width();
    const int h = height();

    int x = cursorPos.x() + CURSOR_OFFSET_X;
    int y = cursorPos.y() + CURSOR_OFFSET_Y;

    if (x + w > geom.right() - EDGE_MARGIN) {
        x = cursorPos.x() - w - CURSOR_OFFSET_X;
    }
    if (y + h > geom.bottom() - EDGE_MARGIN) {
        y = cursorPos.y() - h - CURSOR_OFFSET_Y;
    }

    x = qMax(geom.left() + EDGE_MARGIN, x);
    y = qMax(geom.top() + EDGE_MARGIN, y);

    move(x, y);
}

void PopupWindow::startAutoClose() {
    if (m_pinned) {
        return;
    }
    m_closeTimer->start(m_autoCloseMs);
}

void PopupWindow::setStatusState(const QString &state) {
    m_statusRow->setProperty("popupState", state);
    repolish(m_statusRow);
}

void PopupWindow::updateCopyButton() {
    // Copy is available as soon as text exists — including mid-stream —
    // and on errors that kept a partial result.
    m_copyBtn->setVisible(!m_resultEdit->toPlainText().isEmpty());
}
