#include "ui/pages/wordselect/wordselect_page.h"
#include "ui/shared/theme/theme.h"
#include "shared/string_bridge.h"
#include "ui/shared/widget_utils.h"
#include "domain/model-catalog/language_list.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QFormLayout>
#include <QLabel>
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
    setObjectName(QStringLiteral("page"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(Theme::Space::xxl, Theme::Space::xxl,
                             Theme::Space::xxl, Theme::Space::xxl);
    root->setSpacing(Theme::Space::lg);

    // ── General settings card ─────────────────────────────────────────
    auto *general_card = new QFrame(this);
    general_card->setObjectName(QStringLiteral("settingsSection"));
    auto *general_form = new QVBoxLayout(general_card);
    general_form->setContentsMargins(Theme::Space::xl, Theme::Space::lg,
                                     Theme::Space::xl, Theme::Space::lg);
    general_form->setSpacing(Theme::Space::md);

    auto *general_title = new QLabel(QStringLiteral("General"), general_card);
    general_title->setObjectName(QStringLiteral("sectionTitle"));
    general_form->addWidget(general_title);

    enabled_checkbox_ = new QCheckBox(
        QStringLiteral("Enable word selection translation"), general_card);
    general_form->addWidget(enabled_checkbox_);

    close_to_tray_checkbox_ = new QCheckBox(
        QStringLiteral("Close to system tray instead of quitting"), general_card);
    general_form->addWidget(close_to_tray_checkbox_);

    root->addWidget(general_card);

    // ── Translation settings card ─────────────────────────────────────
    auto *trans_card = new QFrame(this);
    trans_card->setObjectName(QStringLiteral("settingsSection"));
    auto *trans_form = new QFormLayout(trans_card);
    trans_form->setContentsMargins(Theme::Space::xl, Theme::Space::lg,
                                   Theme::Space::xl, Theme::Space::lg);
    trans_form->setSpacing(Theme::Space::md);
    trans_form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto *trans_title = new QLabel(QStringLiteral("Translation"), trans_card);
    trans_title->setObjectName(QStringLiteral("sectionTitle"));
    trans_form->addRow(trans_title);

    target_lang_combo_ = new QComboBox(trans_card);
    for (int i = 0; i < translation_language_count(); ++i) {
        target_lang_combo_->addItem(qtrans::app::from_utf8(translation_languages()[i].label));
    }
    target_lang_combo_->setCurrentIndex(findLanguageIndex(QStringLiteral("Chinese")));
    configureComboBox(target_lang_combo_, 200);

    auto *target_label = new QLabel(QStringLiteral("Target language:"), trans_card);
    target_label->setObjectName(QStringLiteral("formLabel"));
    trans_form->addRow(target_label, target_lang_combo_);

    root->addWidget(trans_card);

    // ── Hotkey & behavior card ────────────────────────────────────────
    auto *behavior_card = new QFrame(this);
    behavior_card->setObjectName(QStringLiteral("settingsSection"));
    auto *behavior_form = new QFormLayout(behavior_card);
    behavior_form->setContentsMargins(Theme::Space::xl, Theme::Space::lg,
                                      Theme::Space::xl, Theme::Space::lg);
    behavior_form->setSpacing(Theme::Space::md);
    behavior_form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto *behavior_title = new QLabel(QStringLiteral("Hotkey & Behavior"), behavior_card);
    behavior_title->setObjectName(QStringLiteral("sectionTitle"));
    behavior_form->addRow(behavior_title);

    hotkey_edit_ = new QLineEdit(behavior_card);
#ifdef Q_OS_MACOS
    hotkey_edit_->setPlaceholderText(QStringLiteral("e.g. Control+`"));
#else
    hotkey_edit_->setPlaceholderText(QStringLiteral("e.g. Ctrl+`"));
#endif
    hotkey_edit_->setClearButtonEnabled(false);

    auto *hotkey_label = new QLabel(QStringLiteral("Shortcut:"), behavior_card);
    hotkey_label->setObjectName(QStringLiteral("formLabel"));
    behavior_form->addRow(hotkey_label, hotkey_edit_);

    auto_close_spin_ = new QSpinBox(behavior_card);
    auto_close_spin_->setRange(1000, 30000);
    auto_close_spin_->setSingleStep(500);
    auto_close_spin_->setSuffix(QStringLiteral(" ms"));
    auto_close_spin_->setValue(5000);

    auto *auto_close_label = new QLabel(QStringLiteral("Auto-close popup after:"), behavior_card);
    auto_close_label->setObjectName(QStringLiteral("formLabel"));
    behavior_form->addRow(auto_close_label, auto_close_spin_);

    root->addWidget(behavior_card);
    root->addStretch(1);

    // ── Connections ───────────────────────────────────────────────────
    connect(enabled_checkbox_, &QCheckBox::toggled,
            this, &WordSelectPage::settingsChanged);
    connect(close_to_tray_checkbox_, &QCheckBox::toggled,
            this, &WordSelectPage::settingsChanged);
    connect(target_lang_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WordSelectPage::settingsChanged);
    connect(hotkey_edit_, &QLineEdit::editingFinished,
            this, &WordSelectPage::settingsChanged);
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
