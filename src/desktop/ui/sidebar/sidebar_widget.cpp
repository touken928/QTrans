#include "ui/sidebar/sidebar_widget.h"
#include "ui/shared/theme/theme.h"
#include "ui/shared/media/image_utils.h"

#include <QButtonGroup>
#include <QEvent>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QResizeEvent>
#include <QScreen>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

namespace {

// ── Icon painting ─────────────────────────────────────────────────────
// Each icon is a 20x20 logical line-art glyph drawn with QPainter at the
// target device pixel ratio. Colors are supplied per mode/state so the
// same path can be tinted for normal, hover, checked, and disabled.

constexpr int kIconSize = Theme::Size::navRailIcon;

QPainterPath bubblePath() {
    QPainterPath path;
    path.addRoundedRect(QRectF(2.0, 3.0, 15.0, 10.5), 3.5, 3.5);
    path.moveTo(5.0, 13.2);
    path.lineTo(5.0, 16.4);
    path.lineTo(9.4, 13.2);
    path.closeSubpath();
    return path;
}

QPainterPath documentPath() {
    QPainterPath path;
    path.moveTo(4.2, 2.0);
    path.lineTo(10.6, 2.0);
    path.lineTo(15.8, 7.2);
    path.lineTo(15.8, 18.0);
    path.lineTo(4.2, 18.0);
    path.closeSubpath();
    path.moveTo(10.6, 2.0);
    path.lineTo(10.6, 7.2);
    path.lineTo(15.8, 7.2);
    return path;
}

QPainterPath chipPath() {
    QPainterPath path;
    path.addRoundedRect(QRectF(4.0, 6.5, 12.0, 7.0), 2.0, 2.0);
    return path;
}

void drawTranslateGlyph(QPainter &p) {
    p.drawPath(bubblePath());
    p.drawLine(QPointF(5.2, 7.2), QPointF(12.4, 7.2));
    p.drawLine(QPointF(5.2, 10.2), QPointF(9.6, 10.2));
}

void drawDocumentsGlyph(QPainter &p) {
    p.drawPath(documentPath());
    p.drawLine(QPointF(6.4, 10.8), QPointF(13.4, 10.8));
    p.drawLine(QPointF(6.4, 13.4), QPointF(11.2, 13.4));
    p.drawLine(QPointF(6.4, 16.0), QPointF(9.0, 16.0));
}

void drawModelsGlyph(QPainter &p) {
    p.drawPath(chipPath());
    p.drawLine(QPointF(5.4, 6.5), QPointF(5.4, 4.4));
    p.drawLine(QPointF(9.0, 6.5), QPointF(9.0, 4.4));
    p.drawLine(QPointF(12.6, 6.5), QPointF(12.6, 4.4));
    p.drawLine(QPointF(5.4, 13.5), QPointF(5.4, 15.6));
    p.drawLine(QPointF(9.0, 13.5), QPointF(9.0, 15.6));
    p.drawLine(QPointF(12.6, 13.5), QPointF(12.6, 15.6));
    p.drawEllipse(QRectF(8.2, 9.0, 3.6, 3.6));
}

void drawPreferencesGlyph(QPainter &p) {
    p.drawLine(QPointF(6.0, 4.2), QPointF(6.0, 15.8));
    p.drawLine(QPointF(10.0, 4.2), QPointF(10.0, 15.8));
    p.drawLine(QPointF(14.0, 4.2), QPointF(14.0, 15.8));
    p.drawEllipse(QRectF(4.0, 5.4, 4.0, 4.0));
    p.drawEllipse(QRectF(8.0, 10.6, 4.0, 4.0));
    p.drawEllipse(QRectF(12.0, 7.4, 4.0, 4.0));
}

QPixmap makeGlyph(PageId page, const QColor &color, qreal dpr) {
    QPixmap pixmap(qRound(kIconSize * dpr), qRound(kIconSize * dpr));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (page) {
        case PageId::Translate:
            drawTranslateGlyph(p);
            break;
        case PageId::Documents:
            drawDocumentsGlyph(p);
            break;
        case PageId::Models:
            drawModelsGlyph(p);
            break;
        case PageId::Preferences:
            drawPreferencesGlyph(p);
            break;
        case PageId::Count:
            break;
    }
    return pixmap;
}

}  // namespace

// ── Public icon factory ────────────────────────────────────────────────

QIcon sidebarNavIcons(PageId page, qreal device_pixel_ratio) {
    namespace C = Theme::Color;
    const qreal dpr = device_pixel_ratio > 0.0 ? device_pixel_ratio : 1.0;

    QIcon icon;
    const QColor on_color(C::surface);  // checked pill is teal → white glyph
    const QColor on_active(C::surface);
    const QColor off_color(C::text);
    const QColor off_active(C::primary);  // hover hint in the accent colour
    const QColor off_disabled(C::textDisabled);

    icon.addPixmap(makeGlyph(page, off_color, dpr), QIcon::Normal, QIcon::Off);
    icon.addPixmap(makeGlyph(page, off_active, dpr), QIcon::Active, QIcon::Off);
    icon.addPixmap(makeGlyph(page, off_disabled, dpr), QIcon::Disabled, QIcon::Off);
    icon.addPixmap(makeGlyph(page, on_color, dpr), QIcon::Normal, QIcon::On);
    icon.addPixmap(makeGlyph(page, on_active, dpr), QIcon::Active, QIcon::On);
    icon.addPixmap(makeGlyph(page, off_disabled, dpr), QIcon::Disabled, QIcon::On);
    return icon;
}

SidebarWidget::SidebarWidget(QWidget *parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("sidebar"));
    setFixedWidth(Theme::Size::sidebarWidth);

    auto *layout = new QVBoxLayout(this);
    // Slim side gutters leave the rail's full width for the brand mark so
    // the logo renders noticeably larger without touching the nav buttons.
    layout->setContentsMargins(Theme::Space::sm, Theme::Space::lg,
                               Theme::Space::sm, Theme::Space::md);
    layout->setSpacing(Theme::Space::xs);

    // ── Brand mark ───────────────────────────────────────────────────
    logo_label_ = new QLabel(this);
    logo_label_->setObjectName(QStringLiteral("sidebarLogo"));
    logo_label_->setAlignment(Qt::AlignCenter);
    logo_label_->setContentsMargins(0, 0, 0, 0);
    logo_label_->setAccessibleName(QStringLiteral("QTrans"));
    layout->addWidget(logo_label_, 0, Qt::AlignHCenter);
    layout->addSpacing(Theme::Space::sm);

    // ── Top navigation group ─────────────────────────────────────────
    auto *nav_group = new QButtonGroup(this);
    nav_group->setExclusive(true);

    translate_button_ = makeNavButton(QStringLiteral("Translate"),
                                      sidebarNavIcons(PageId::Translate, 1.0));
    translate_button_->setChecked(true);
    layout->addWidget(translate_button_, 0, Qt::AlignHCenter);
    nav_group->addButton(translate_button_, static_cast<int>(PageId::Translate));

    documents_button_ = makeNavButton(QStringLiteral("Documents"),
                                      sidebarNavIcons(PageId::Documents, 1.0));
    layout->addWidget(documents_button_, 0, Qt::AlignHCenter);
    nav_group->addButton(documents_button_, static_cast<int>(PageId::Documents));

    models_button_ = makeNavButton(QStringLiteral("Models"),
                                   sidebarNavIcons(PageId::Models, 1.0));
    layout->addWidget(models_button_, 0, Qt::AlignHCenter);
    nav_group->addButton(models_button_, static_cast<int>(PageId::Models));

    layout->addStretch(1);

    // ── Bottom action: Preferences ───────────────────────────────────
    preferences_button_ = makeNavButton(QStringLiteral("Preferences"),
                                        sidebarNavIcons(PageId::Preferences, 1.0));
    layout->addWidget(preferences_button_, 0, Qt::AlignHCenter);
    nav_group->addButton(preferences_button_, static_cast<int>(PageId::Preferences));

    connect(nav_group, &QButtonGroup::idClicked, this,
            [this](int id) {
                if (id >= 0 && id < static_cast<int>(PageId::Count)) {
                    emit pageSelected(static_cast<PageId>(id));
                }
            });

    refreshLogo();
    refreshIcons();
}

QToolButton *SidebarWidget::makeNavButton(const QString &text, const QIcon &icon) {
    auto *button = new QToolButton(this);
    button->setObjectName(QStringLiteral("navButton"));
    button->setIcon(icon);
    button->setIconSize(QSize(kIconSize, kIconSize));
    button->setFixedSize(Theme::Size::navItemHeight, Theme::Size::navItemHeight);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    button->setCheckable(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::StrongFocus);
    // Tooltips appear even while the app window is unfocused (a common
    // macOS Qt quirk); the fixed geometry above guarantees the hover state
    // never shifts the rail layout.
    button->setAttribute(Qt::WA_AlwaysShowToolTips);
    button->setToolTip(text);
    button->setAccessibleName(text);
    button->setAccessibleDescription(
        QStringLiteral("Switch to the %1 page").arg(text));
    return button;
}

void SidebarWidget::setCurrentPage(PageId page) {
    translate_button_->setChecked(page == PageId::Translate);
    documents_button_->setChecked(page == PageId::Documents);
    models_button_->setChecked(page == PageId::Models);
    preferences_button_->setChecked(page == PageId::Preferences);
}

void SidebarWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    refreshLogo();
}

bool SidebarWidget::event(QEvent *event) {
    if (event->type() == QEvent::Show || event->type() == QEvent::ScreenChangeInternal) {
        refreshLogo();
        refreshIcons();
    }
    return QWidget::event(event);
}

void SidebarWidget::refreshIcons() {
    const qreal dpr = (windowHandle() != nullptr ? windowHandle()->screen()
                                                 : QGuiApplication::primaryScreen())
                          ->devicePixelRatio();
    translate_button_->setIcon(sidebarNavIcons(PageId::Translate, dpr));
    documents_button_->setIcon(sidebarNavIcons(PageId::Documents, dpr));
    models_button_->setIcon(sidebarNavIcons(PageId::Models, dpr));
    preferences_button_->setIcon(sidebarNavIcons(PageId::Preferences, dpr));
}

void SidebarWidget::refreshLogo() {
    if (logo_label_ == nullptr || width() <= 0) {
        return;
    }

    const int logo_width = qMax(1, width() - (Theme::Space::sm * 2));
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
