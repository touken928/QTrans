#include "wordselect/popup_window.h"

#include <QApplication>
#include <QCursor>
#include <QFrame>
#include <QLabel>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

PopupWindow::PopupWindow(QWidget *parent)
    : QWidget(parent) {
    setWindowFlags(
        Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::ToolTip | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_DeleteOnClose, false);

    setMinimumWidth(MIN_WIDTH);
    setMaximumWidth(MAX_WIDTH);

    setupUI();

    m_closeTimer = new QTimer(this);
    m_closeTimer->setSingleShot(true);
    connect(m_closeTimer, &QTimer::timeout, this, &QWidget::hide);
}

void PopupWindow::setupUI() {
    m_frame = new QFrame(this);
    m_frame->setObjectName(QStringLiteral("popupFrame"));

    auto *layout = new QVBoxLayout(m_frame);
    layout->setContentsMargins(14, 12, 14, 8);
    layout->setSpacing(6);

    m_resultLabel = new QLabel(QStringLiteral(""), m_frame);
    m_resultLabel->setObjectName(QStringLiteral("popupResult"));
    m_resultLabel->setWordWrap(true);
    m_resultLabel->setMaximumWidth(MAX_WIDTH - 28);
    m_resultLabel->setTextFormat(Qt::PlainText);
    layout->addWidget(m_resultLabel);

    m_statusLabel = new QLabel(QStringLiteral(""), m_frame);
    m_statusLabel->setObjectName(QStringLiteral("popupStatus"));
    layout->addWidget(m_statusLabel);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(m_frame);

    m_frame->setStyleSheet(QStringLiteral(R"(
        QFrame#popupFrame {
            background-color: #ffffff;
            border: 1px solid #d1d1d6;
            border-radius: 8px;
        }
    )"));

    m_resultLabel->setStyleSheet(QStringLiteral(R"(
        QLabel#popupResult {
            color: #1d1d1f;
            font-size: 14px;
            padding: 2px 0px;
        }
    )"));

    m_statusLabel->setStyleSheet(QStringLiteral(R"(
        QLabel#popupStatus {
            color: #6e6e73;
            font-size: 11px;
            padding: 2px 0px;
        }
    )"));
}

void PopupWindow::showLoading(const QString &sourceText) {
    Q_UNUSED(sourceText);
    m_isStreaming = true;

    m_resultLabel->setText(QStringLiteral("Translating\u2026"));
    m_resultLabel->setStyleSheet(QStringLiteral(R"(
        QLabel#popupResult {
            color: #0071e3;
            font-size: 14px;
            padding: 2px 0px;
        }
    )"));

    m_statusLabel->setText(QStringLiteral("AI Translating\u2026"));

    positionNearCursor();
    show();
    raise();
}

void PopupWindow::appendChunk(const QString &chunk) {
    if (!m_isStreaming) return;

    QString current = m_resultLabel->text();
    static const QString placeholder = QStringLiteral("Translating\u2026");
    if (current == placeholder) {
        m_resultLabel->setText(chunk);
    } else {
        m_resultLabel->setText(current + chunk);
    }
    adjustPopupSize();
}

void PopupWindow::finishStreaming() {
    if (!m_isStreaming) return;

    m_isStreaming = false;

    m_resultLabel->setStyleSheet(QStringLiteral(R"(
        QLabel#popupResult {
            color: #1d1d1f;
            font-size: 14px;
            padding: 2px 0px;
        }
    )"));

    m_statusLabel->setText(QStringLiteral("AI Translate"));
    adjustPopupSize();
    startAutoClose();
}

void PopupWindow::showError(const QString &message) {
    m_isStreaming = false;

    m_resultLabel->setText(message);
    m_resultLabel->setStyleSheet(QStringLiteral(R"(
        QLabel#popupResult {
            color: #ff3b30;
            font-size: 13px;
            padding: 2px 0px;
        }
    )"));

    m_statusLabel->setText(QStringLiteral("Error"));
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

void PopupWindow::enterEvent(QEnterEvent *event) {
    m_closeTimer->stop();
    QWidget::enterEvent(event);
}

void PopupWindow::leaveEvent(QEvent *event) {
    if (!m_isStreaming) {
        startAutoClose();
    }
    QWidget::leaveEvent(event);
}

void PopupWindow::hideEvent(QHideEvent *event) {
    m_isStreaming = false;
    m_closeTimer->stop();
    emit dismissed();
    QWidget::hideEvent(event);
}

void PopupWindow::positionNearCursor() {
    const QPoint cursorPos = QCursor::pos();
    QScreen *screen = QApplication::screenAt(cursorPos);
    if (!screen) {
        screen = QApplication::primaryScreen();
    }
    if (!screen) return;

    const QRect geom = screen->availableGeometry();
    adjustPopupSize();

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
    m_closeTimer->start(m_autoCloseMs);
}

void PopupWindow::adjustPopupSize() {
    adjustSize();
}
