#include "app/ui/wordselect_page.h"

#include "app/string_bridge.h"
#include "app/widget_utils.h"
#include "translation/translation_languages.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

int findLanguageIndex(const QString &model_name) {
    for (int i = 0; i < translation_language_count(); ++i) {
        if (qtrans::app::from_utf8(translation_languages()[i].model_name) == model_name) {
            return i;
        }
    }
    return 0;
}

QString modelNameAt(const QComboBox *combo) {
    const int index = combo->currentIndex();
    if (index < 0 || index >= translation_language_count()) {
        return QStringLiteral("English");
    }
    return qtrans::app::from_utf8(translation_languages()[index].model_name);
}

}  // namespace

WordSelectPage::WordSelectPage(QWidget *parent)
    : QWidget(parent) {
    auto *form = new QFormLayout(this);
    form->setContentsMargins(16, 16, 16, 16);
    form->setSpacing(10);

    enabled_checkbox_ = new QCheckBox(QStringLiteral("Enable word selection translation"), this);
    form->addRow(enabled_checkbox_);

    close_to_tray_checkbox_ = new QCheckBox(QStringLiteral("Close to system tray instead of quitting"), this);
    form->addRow(close_to_tray_checkbox_);

    target_lang_combo_ = new QComboBox(this);
    for (int i = 0; i < translation_language_count(); ++i) {
        target_lang_combo_->addItem(qtrans::app::from_utf8(translation_languages()[i].label));
    }
    target_lang_combo_->setCurrentIndex(findLanguageIndex(QStringLiteral("Chinese")));
    configureComboBox(target_lang_combo_, 180);
    form->addRow(QStringLiteral("Target language:"), target_lang_combo_);

    hotkey_edit_ = new QLineEdit(this);
#ifdef Q_OS_MACOS
    hotkey_edit_->setPlaceholderText(QStringLiteral("e.g. Control+`"));
#else
    hotkey_edit_->setPlaceholderText(QStringLiteral("e.g. Ctrl+`"));
#endif
    hotkey_edit_->setClearButtonEnabled(false);
    form->addRow(QStringLiteral("Shortcut:"), hotkey_edit_);

    auto_close_spin_ = new QSpinBox(this);
    auto_close_spin_->setRange(1000, 30000);
    auto_close_spin_->setSingleStep(500);
    auto_close_spin_->setSuffix(QStringLiteral(" ms"));
    auto_close_spin_->setValue(5000);
    form->addRow(QStringLiteral("Auto-close popup after:"), auto_close_spin_);

    connect(enabled_checkbox_, &QCheckBox::toggled, this, &WordSelectPage::settingsChanged);
    connect(close_to_tray_checkbox_, &QCheckBox::toggled, this, &WordSelectPage::settingsChanged);
    connect(target_lang_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WordSelectPage::settingsChanged);
    connect(hotkey_edit_, &QLineEdit::editingFinished, this, &WordSelectPage::settingsChanged);
    connect(auto_close_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &WordSelectPage::settingsChanged);
}

void WordSelectPage::setEnabled(bool enabled) {
    enabled_checkbox_->setChecked(enabled);
}

void WordSelectPage::setCloseToTray(bool close_to_tray) {
    close_to_tray_checkbox_->setChecked(close_to_tray);
}

void WordSelectPage::setTargetLanguage(const QString &model_name) {
    const int idx = findLanguageIndex(model_name);
    if (idx >= 0) {
        target_lang_combo_->setCurrentIndex(idx);
    }
}

void WordSelectPage::setHotkey(const QString &shortcut) {
    hotkey_edit_->setText(shortcut);
}

void WordSelectPage::setAutoCloseMs(int ms) {
    auto_close_spin_->setValue(ms);
}

bool WordSelectPage::isEnabled() const {
    return enabled_checkbox_->isChecked();
}

bool WordSelectPage::isCloseToTray() const {
    return close_to_tray_checkbox_->isChecked();
}

QString WordSelectPage::targetLanguage() const {
    return modelNameAt(target_lang_combo_);
}

QString WordSelectPage::hotkey() const {
    return hotkey_edit_->text().trimmed();
}

int WordSelectPage::autoCloseMs() const {
    return auto_close_spin_->value();
}
