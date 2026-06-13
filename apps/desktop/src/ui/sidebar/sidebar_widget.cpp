#include "ui/sidebar/sidebar_widget.h"
#include "ui/shared/theme/theme.h"
#include "ui/shared/media/image_utils.h"

#include <QButtonGroup>
#include <QEvent>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QVBoxLayout>
#include <QWindow>

SidebarWidget::SidebarWidget(QWidget *parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("sidebar"));
    setFixedWidth(Theme::Size::sidebarWidth);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(Theme::Space::md, Theme::Space::xl,
                               Theme::Space::md, Theme::Space::xl);
    layout->setSpacing(Theme::Space::xs);

    // ── Logo ──────────────────────────────────────────────────────────
    logo_label_ = new QLabel(this);
    logo_label_->setObjectName(QStringLiteral("sidebarLogo"));
    logo_label_->setAlignment(Qt::AlignCenter);
    logo_label_->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(logo_label_, 0, Qt::AlignHCenter);
    layout->addSpacing(Theme::Space::lg);

    // ── Nav items ─────────────────────────────────────────────────────
    translate_button_ = new QPushButton(
        QString(Theme::NavIcon::translate) + QStringLiteral("  Translate"), this);
    translate_button_->setObjectName(QStringLiteral("navButton"));
    translate_button_->setCheckable(true);
    translate_button_->setChecked(true);
    translate_button_->setCursor(Qt::PointingHandCursor);
    layout->addWidget(translate_button_);

    wordselect_button_ = new QPushButton(
        QString(Theme::NavIcon::wordSelect) + QStringLiteral("  Word Select"), this);
    wordselect_button_->setObjectName(QStringLiteral("navButton"));
    wordselect_button_->setCheckable(true);
    wordselect_button_->setCursor(Qt::PointingHandCursor);
    layout->addWidget(wordselect_button_);

    model_button_ = new QPushButton(
        QString(Theme::NavIcon::model) + QStringLiteral("  Model"), this);
    model_button_->setObjectName(QStringLiteral("navButton"));
    model_button_->setCheckable(true);
    model_button_->setCursor(Qt::PointingHandCursor);
    layout->addWidget(model_button_);

    auto *nav_group = new QButtonGroup(this);
    nav_group->setExclusive(true);
    nav_group->addButton(translate_button_, 0);
    nav_group->addButton(wordselect_button_, 1);
    nav_group->addButton(model_button_, 2);

    connect(nav_group, &QButtonGroup::idClicked, this, &SidebarWidget::pageSelected);

    layout->addStretch(1);
    refreshLogo();
}

void SidebarWidget::setCurrentPage(int index) {
    translate_button_->setChecked(index == 0);
    wordselect_button_->setChecked(index == 1);
    model_button_->setChecked(index == 2);
}

void SidebarWidget::setNavigationEnabled(bool enabled) {
    translate_button_->setEnabled(enabled);
    wordselect_button_->setEnabled(enabled);
    model_button_->setEnabled(enabled);
}

void SidebarWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    refreshLogo();
}

bool SidebarWidget::event(QEvent *event) {
    if (event->type() == QEvent::Show || event->type() == QEvent::ScreenChangeInternal) {
        refreshLogo();
    }
    return QWidget::event(event);
}

void SidebarWidget::refreshLogo() {
    if (logo_label_ == nullptr || width() <= 0) {
        return;
    }

    const int logo_width = qMax(1, width() - (Theme::Space::md * 2));
    const QImage source(QStringLiteral(":/branding/logo.png"));
    if (source.isNull()) {
        logo_label_->setText(QStringLiteral("QTrans"));
        return;
    }

    const QImage cropped = trimNearSolidBorder(source);
    const QPixmap pixmap = scaledPixmapForWidth(cropped, logo_width,
                                                (windowHandle() != nullptr ? windowHandle()->screen()
                                                                           : QGuiApplication::primaryScreen())
                                                    ->devicePixelRatio());
    if (pixmap.isNull()) {
        logo_label_->setText(QStringLiteral("QTrans"));
        return;
    }

    logo_label_->clear();
    logo_label_->setPixmap(pixmap);
    logo_label_->setFixedSize(pixmap.size() / pixmap.devicePixelRatioF());
}
