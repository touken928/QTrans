#pragma once

#include <QString>

class QWidget;

// AppTheme  — Centralised stylesheet factory.
//
// Every public function composes a complete Qt stylesheet from the
// design tokens in Theme::.  Call apply() once on MainWindow to style
// the entire widget tree; use the other helpers for overlay panels and
// pop-up windows that need their own stylesheet context.
// ──────────────────────────────────────────────────────────────────────

namespace AppTheme {

QString applicationStyleSheet();
QString modalOverlayStyleSheet();
QString modalPanelStyleSheet();
QString popupWindowStyleSheet();

void apply(QWidget *widget);
void applyPopup(QWidget *widget);

}  // namespace AppTheme
