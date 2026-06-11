#include "app/modal_overlay.h"
#include "app/ui/app_theme.h"

#include <QEvent>
#include <QFrame>
#include <QtGlobal>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

ModalOverlay::ModalOverlay(QWidget *parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("modalOverlay"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet(AppTheme::modalOverlayStyleSheet());
    hide();

    panel_ = new QFrame(this);
    panel_->setObjectName(QStringLiteral("modalPanel"));
    panel_->setAutoFillBackground(true);
    panel_->setStyleSheet(AppTheme::modalPanelStyleSheet());

    content_layout_ = new QVBoxLayout(panel_);
    content_layout_->setContentsMargins(24, 24, 24, 24);
    content_layout_->setSpacing(14);

    if (parent != nullptr) {
        parent->installEventFilter(this);
    }
}

void ModalOverlay::setContent(QWidget *content, const QSize &preferred_size) {
    preferred_size_ = preferred_size;

    if (content_widget_ != nullptr) {
        content_layout_->removeWidget(content_widget_);
        delete content_widget_;
        content_widget_ = nullptr;
    }

    content_widget_ = content;
    if (content_widget_ != nullptr) {
        content_widget_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        content_layout_->addWidget(content_widget_);
    }

    panel_->setMinimumSize(preferred_size_);
    panel_->resize(preferred_size_);
    repositionPanel();
}

void ModalOverlay::showModal() {
    if (parentWidget() != nullptr) {
        setGeometry(parentWidget()->rect());
        raise();
        setFocus();
    }
    show();
    repositionPanel();

    QTimer::singleShot(0, this, [this]() {
        if (content_widget_ != nullptr) {
            const QSize hint = content_widget_->sizeHint().expandedTo(preferred_size_);
            const int max_w = qMax(320, width() - 32);
            const int max_h = qMax(240, height() - 32);
            panel_->resize(qMin(hint.width(), max_w), qMin(hint.height(), max_h));
        }
        repositionPanel();
    });
}

void ModalOverlay::hideModal() {
    hide();
}

void ModalOverlay::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    repositionPanel();
}

bool ModalOverlay::eventFilter(QObject *watched, QEvent *event) {
    if (watched == parentWidget() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
        repositionPanel();
    }
    return QWidget::eventFilter(watched, event);
}

void ModalOverlay::repositionPanel() {
    if (panel_ == nullptr) {
        return;
    }

    const int max_w = qMax(320, width() - 32);
    const int max_h = qMax(240, height() - 32);
    panel_->setMaximumSize(max_w, max_h);

    const int x = (width() - panel_->width()) / 2;
    const int y = (height() - panel_->height()) / 2;
    panel_->move(qMax(16, x), qMax(16, y));
}
