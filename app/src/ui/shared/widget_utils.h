#pragma once

#include <QString>

class QComboBox;

QString comboBoxStyleSheet();

void configureComboBox(QComboBox *combo, int minimum_width = 0);
