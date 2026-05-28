#include "app/widget_utils.h"

#include "app/ui/app_theme.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFontMetrics>
#include <QListView>
#include <QtGlobal>

QString comboBoxStyleSheet() {
    return AppTheme::applicationStyleSheet();
}

namespace {

int comboPopupWidth(const QComboBox *combo) {
    int width = combo->minimumWidth();
    const QFontMetrics metrics(combo->font());
    for (int i = 0; i < combo->count(); ++i) {
        width = qMax(width, metrics.horizontalAdvance(combo->itemText(i)) + 48);
    }
    return width;
}

}  // namespace

void configureComboBox(QComboBox *combo, int minimum_width) {
    if (combo == nullptr) {
        return;
    }

    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(8);
    combo->setMaxVisibleItems(12);
    combo->setInsertPolicy(QComboBox::NoInsert);

    if (minimum_width > 0) {
        combo->setMinimumWidth(minimum_width);
    }

    if (auto *list = qobject_cast<QListView *>(combo->view())) {
        list->setTextElideMode(Qt::ElideNone);
        list->setSpacing(2);
        list->setUniformItemSizes(false);
        list->setMinimumWidth(comboPopupWidth(combo));
    }
}
