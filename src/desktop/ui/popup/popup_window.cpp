#include "ui/popup/popup_window.h"
#include "ui/shared/theme/app_theme.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>

PopupWindow::PopupWindow(QWidget *parent)
    : QWidget(parent) {
    setWindowFlags(
        Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::ToolTip);
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

    auto *bottomRow = new QHBoxLayout();
    bottomRow->setContentsMargins(0, 0, 0, 0);
    bottomRow->setSpacing(8);

    m_statusLabel = new QLabel(QStringLiteral(""), m_frame);
    m_statusLabel->setObjectName(QStringLiteral("popupStatus"));
    bottomRow->addWidget(m_statusLabel, 1);

    m_copyBtn = new QPushButton(QStringLiteral("Copy"), m_frame);
    m_copyBtn->setObjectName(QStringLiteral("popupCopyBtn"));
    m_copyBtn->setFixedHeight(22);
    m_copyBtn->setVisible(false);
    bottomRow->addWidget(m_copyBtn);

    m_closeBtn = new QPushButton(QStringLiteral("\u2715"), m_frame);
    m_closeBtn->setObjectName(QStringLiteral("popupCloseBtn"));
    m_closeBtn->setFixedSize(22, 22);
    bottomRow->addWidget(m_closeBtn);

    layout->addLayout(bottomRow);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(m_frame);

    // Apply shared theme stylesheet instead of hardcoded QSS
    AppTheme::applyPopup(this);

    connect(m_closeBtn, &QPushButton::clicked, this, &PopupWindow::onCloseClicked);
    connect(m_copyBtn, &QPushButton::clicked, this, &PopupWindow::onCopyClicked);
}

void PopupWindow::showLoading(const QString &sourceText) {
    Q_UNUSED(sourceText);
    m_isStreaming = true;

    m_resultLabel->setText(QStringLiteral("Translating\u2026"));
    m_statusLabel->setText(QStringLiteral("AI Translating\u2026"));
    m_copyBtn->setVisible(false);

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
    m_statusLabel->setText(QStringLiteral("AI Translate"));
    m_copyBtn->setVisible(true);
    adjustPopupSize();
    startAutoClose();
}

void PopupWindow::showError(const QString &message) {
    m_isStreaming = false;

    m_resultLabel->setText(message);
    m_statusLabel->setText(QStringLiteral("Error"));
    m_copyBtn->setVisible(false);

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

void PopupWindow::onCloseClicked() {
    m_closeTimer->stop();
    hide();
}

void PopupWindow::onCopyClicked() {
    QApplication::clipboard()->setText(m_resultLabel->text());
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
