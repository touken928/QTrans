#include "app/ui/sidebar_widget.h"

#include "app/ui/image_utils.h"

#include <QButtonGroup>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QVBoxLayout>
#include <QWindow>

namespace {

constexpr int kSidebarWidth = 180;
constexpr int kHorizontalMargin = 12;

} // namespace

SidebarWidget::SidebarWidget(QWidget * parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("sidebar"));
    setFixedWidth(kSidebarWidth);

    auto * layout = new QVBoxLayout(this);
    layout->setContentsMargins(kHorizontalMargin, 16, kHorizontalMargin, 16);
    layout->setSpacing(8);

    logo_label_ = new QLabel(this);
    logo_label_->setObjectName(QStringLiteral("sidebarLogo"));
    logo_label_->setAlignment(Qt::AlignCenter);
    logo_label_->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(logo_label_, 0, Qt::AlignHCenter);
    layout->addSpacing(12);

    translate_button_ = new QPushButton(QStringLiteral("Translate"), this);
    translate_button_->setObjectName(QStringLiteral("navButton"));
    translate_button_->setCheckable(true);
    translate_button_->setChecked(true);
    layout->addWidget(translate_button_);

    model_button_ = new QPushButton(QStringLiteral("Model"), this);
    model_button_->setObjectName(QStringLiteral("navButton"));
    model_button_->setCheckable(true);
    layout->addWidget(model_button_);

    auto * nav_group = new QButtonGroup(this);
    nav_group->setExclusive(true);
    nav_group->addButton(translate_button_, 0);
    nav_group->addButton(model_button_, 1);

    connect(nav_group, &QButtonGroup::idClicked, this, &SidebarWidget::pageSelected);

    layout->addStretch(1);
    refreshLogo();
}

void SidebarWidget::setCurrentPage(int index) {
    translate_button_->setChecked(index == 0);
    model_button_->setChecked(index == 1);
}

void SidebarWidget::setNavigationEnabled(bool enabled) {
    translate_button_->setEnabled(enabled);
    model_button_->setEnabled(enabled);
}

void SidebarWidget::resizeEvent(QResizeEvent * event) {
    QWidget::resizeEvent(event);
    refreshLogo();
}

bool SidebarWidget::event(QEvent * event) {
    if (event->type() == QEvent::Show || event->type() == QEvent::ScreenChangeInternal) {
        refreshLogo();
    }
    return QWidget::event(event);
}

void SidebarWidget::refreshLogo() {
    if (logo_label_ == nullptr || width() <= 0) {
        return;
    }

    const int logo_width = qMax(1, width() - (kHorizontalMargin * 2));
    const QImage source(QStringLiteral(":/branding/logo.png"));
    if (source.isNull()) {
        logo_label_->setText(QStringLiteral("QTrans"));
        return;
    }

    const QImage cropped = trimNearSolidBorder(source);
    const QPixmap pixmap = scaledPixmapForWidth(cropped, logo_width, currentDevicePixelRatio());
    if (pixmap.isNull()) {
        logo_label_->setText(QStringLiteral("QTrans"));
        return;
    }

    logo_label_->clear();
    logo_label_->setPixmap(pixmap);
    logo_label_->setFixedSize(pixmap.size() / pixmap.devicePixelRatioF());
}

qreal SidebarWidget::currentDevicePixelRatio() const {
    const QWindow * native_window = windowHandle();
    const QScreen * screen = native_window != nullptr ? native_window->screen() : nullptr;
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }
    return screen != nullptr ? screen->devicePixelRatio() : 1.0;
}
