#pragma once

#include <QWidget>

class QFrame;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTimer;

// Word-select result popup. A bounded source preview sits above a
// scrollable translation output; the status row tints by phase
// (translating / done / error) and offers Copy while text is available —
// even mid-stream — plus Retry when a job fails.
//
// Dismissal contract: Escape dismisses whenever the popup is visible, and
// the close button (and auto-close / pin) complete the affordances. Because
// the popup never activates (WA_ShowWithoutActivating + ToolTip window type
// are kept, no blind window-flag change), keyboard input usually stays with
// the front app, so while the popup is visible a narrowly scoped set of
// listeners is installed — a Qt application event filter on every platform,
// plus a global keyboard monitor on macOS (CGEventTap, needs the
// accessibility trust word-select already requires) and Windows
// (WH_KEYBOARD_LL, no privileges) — and removed again in hideEvent. The
// popup's own keyPressEvent remains a last-resort fallback. A modal dialog
// in the main window always keeps its Escape.
class PopupWindow : public QWidget {
    Q_OBJECT

public:
    explicit PopupWindow(QWidget *parent = nullptr);

    void showLoading(const QString &sourceText);
    void appendChunk(const QString &chunk);
    void finishStreaming();
    void showError(const QString &message);

    void setAutoCloseMs(int ms);
    int autoCloseMs() const;

    bool isStreaming() const;
    bool isPinned() const;

    // Dismisses the popup as if the user pressed Escape (or the close
    // button): stops the auto-close timer and hides, which also tears down
    // the Escape listeners and emits dismissed(). Safe to call when the
    // popup is already hidden.
    void dismissByEscape();

signals:
    void dismissed();
    void retryRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:
    void onCloseClicked();
    void onCopyClicked();
    void onRetryClicked();
    void onPinToggled(bool pinned);

private:
    void setupUI();
    void positionNearCursor();
    void startAutoClose();
    void setStatusState(const QString &state);
    void updateCopyButton();
    void installEscapeMonitors();
    void uninstallEscapeMonitors();

    QFrame *m_frame = nullptr;
    QFrame *m_sourceBox = nullptr;
    QLabel *m_sourceLabel = nullptr;
    QWidget *m_statusRow = nullptr;
    QLabel *m_statusDot = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPlainTextEdit *m_resultEdit = nullptr;
    QPushButton *m_closeBtn = nullptr;
    QPushButton *m_copyBtn = nullptr;
    QPushButton *m_pinBtn = nullptr;
    QPushButton *m_retryBtn = nullptr;
    QTimer *m_closeTimer = nullptr;

    int m_autoCloseMs = 5000;
    bool m_isStreaming = false;
    bool m_pinned = false;
    // Guards install/remove symmetry for the Escape listeners across the
    // show/hide lifecycle (a re-show must not double-install, and an
    // unrelated hide must not tear down a listener another show owns).
    bool m_escapeMonitorsInstalled = false;

    static constexpr int CURSOR_OFFSET_X = 20;
    static constexpr int CURSOR_OFFSET_Y = 20;
    static constexpr int EDGE_MARGIN = 10;
    static constexpr int MAX_WIDTH = 480;
    static constexpr int MIN_WIDTH = 200;
    static constexpr int MAX_HEIGHT = 420;
};
