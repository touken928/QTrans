#pragma once

#include <QString>

class QWidget;

namespace AppTheme {

QString applicationStyleSheet();
QString modalOverlayStyleSheet();
QString modalPanelStyleSheet();

void apply(QWidget *widget);

}  // namespace AppTheme
