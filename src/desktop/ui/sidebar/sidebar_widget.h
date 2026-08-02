#pragma once

#include "ui/shell/page_id.h"

#include <QIcon>
#include <QWidget>

class QLabel;
class QToolButton;

// 72px icon navigation rail. Translate / Documents / Models sit at the
// top; Preferences is pinned to the bottom. Every item is an icon-only
// checkable tool button inside an exclusive group: keyboard arrow
// navigation, tooltips, and accessible names are wired for each entry.
// The rail emits typed PageId values — no numeric stack indexes.
class SidebarWidget : public QWidget {
    Q_OBJECT

public:
    explicit SidebarWidget(QWidget *parent = nullptr);

    void setCurrentPage(PageId page);

signals:
    void pageSelected(PageId page);

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool event(QEvent *event) override;

private:
    QToolButton *makeNavButton(const QString &text, const QIcon &icon);
    void refreshLogo();
    void refreshIcons();

    QLabel *logo_label_ = nullptr;
    QToolButton *translate_button_ = nullptr;
    QToolButton *documents_button_ = nullptr;
    QToolButton *models_button_ = nullptr;
    QToolButton *preferences_button_ = nullptr;
};

// Builds the monochrome line-art icon set for the rail. Icons are drawn
// with QPainter into device-pixel-ratio-aware pixmaps and registered for
// every QIcon mode/state so checked/hover/disabled states recolor without
// any stylesheet trickery or new assets.
QIcon sidebarNavIcons(PageId page, qreal device_pixel_ratio);
