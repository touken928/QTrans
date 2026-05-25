#include "app/ui/wordselect_page.h"

#include "app/widget_utils.h"
#include "translation/translation_languages.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

int findLanguageIndex(const QString & model_name) {
    for (int i = 0; i < translation_language_count(); ++i) {
        if (QString::fromUtf8(translation_languages()[i].model_name) == model_name) {
            return i;
        }
    }
    return 0;
}

QString modelNameAt(const QComboBox * combo) {
    const int index = combo->currentIndex();
    if (index < 0 || index >= translation_language_count()) {
        return QStringLiteral("English");
    }
    return QString::fromUtf8(translation_languages()[index].model_name);
}

} // namespace

WordSelectPage::WordSelectPage(QWidget * parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("page"));

    auto * root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    auto * header = new QLabel(QStringLiteral("划词翻译设置"), this);
    header->setObjectName(QStringLiteral("panelLabel"));
    QFont header_font = header->font();
    header_font.setPointSize(14);
    header_font.setBold(true);
    header->setFont(header_font);
    root->addWidget(header);

    auto * general_group = new QGroupBox(QStringLiteral("常规"), this);
    auto * general_layout = new QVBoxLayout(general_group);
    general_layout->setSpacing(8);

    enabled_checkbox_ = new QCheckBox(QStringLiteral("启用划词翻译"), general_group);
    general_layout->addWidget(enabled_checkbox_);

    root->addWidget(general_group);

    auto * lang_group = new QGroupBox(QStringLiteral("语言设置"), this);
    auto * lang_layout = new QFormLayout(lang_group);
    lang_layout->setSpacing(8);

    source_lang_combo_ = new QComboBox(lang_group);
    for (int i = 0; i < translation_language_count(); ++i) {
        source_lang_combo_->addItem(QString::fromUtf8(translation_languages()[i].label));
    }
    configureComboBox(source_lang_combo_, 180);
    lang_layout->addRow(QStringLiteral("源语言:"), source_lang_combo_);

    target_lang_combo_ = new QComboBox(lang_group);
    for (int i = 0; i < translation_language_count(); ++i) {
        target_lang_combo_->addItem(QString::fromUtf8(translation_languages()[i].label));
    }
    configureComboBox(target_lang_combo_, 180);
    lang_layout->addRow(QStringLiteral("目标语言:"), target_lang_combo_);

    root->addWidget(lang_group);

    auto * hotkey_group = new QGroupBox(QStringLiteral("快捷键与弹窗"), this);
    auto * hotkey_layout = new QFormLayout(hotkey_group);
    hotkey_layout->setSpacing(8);

    hotkey_edit_ = new QLineEdit(hotkey_group);
    hotkey_edit_->setPlaceholderText(QStringLiteral("如 Ctrl+`"));
    hotkey_edit_->setClearButtonEnabled(false);
    hotkey_layout->addRow(QStringLiteral("快捷键:"), hotkey_edit_);

    auto_close_spin_ = new QSpinBox(hotkey_group);
    auto_close_spin_->setRange(1000, 30000);
    auto_close_spin_->setSingleStep(500);
    auto_close_spin_->setSuffix(QStringLiteral(" ms"));
    auto_close_spin_->setValue(5000);
    hotkey_layout->addRow(QStringLiteral("弹窗自动关闭:"), auto_close_spin_);

    auto * hint_label = new QLabel(
        QStringLiteral("划词翻译使用与主界面相同的翻译模型和提示词模板。选中文本后按快捷键即可翻译。"),
        hotkey_group);
    hint_label->setWordWrap(true);
    hint_label->setObjectName(QStringLiteral("hintLabel"));
    hotkey_layout->addRow(hint_label);

    root->addWidget(hotkey_group);

    root->addStretch(1);

    connect(enabled_checkbox_, &QCheckBox::toggled, this, &WordSelectPage::settingsChanged);
    connect(source_lang_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WordSelectPage::settingsChanged);
    connect(target_lang_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &WordSelectPage::settingsChanged);
    connect(hotkey_edit_, &QLineEdit::editingFinished, this, &WordSelectPage::settingsChanged);
    connect(auto_close_spin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &WordSelectPage::settingsChanged);
}

void WordSelectPage::setEnabled(bool enabled) {
    enabled_checkbox_->setChecked(enabled);
}

void WordSelectPage::setSourceLanguage(const QString & model_name) {
    const int idx = findLanguageIndex(model_name);
    if (idx >= 0) {
        source_lang_combo_->setCurrentIndex(idx);
    }
}

void WordSelectPage::setTargetLanguage(const QString & model_name) {
    const int idx = findLanguageIndex(model_name);
    if (idx >= 0) {
        target_lang_combo_->setCurrentIndex(idx);
    }
}

void WordSelectPage::setHotkey(const QString & shortcut) {
    hotkey_edit_->setText(shortcut);
}

void WordSelectPage::setAutoCloseMs(int ms) {
    auto_close_spin_->setValue(ms);
}

bool WordSelectPage::isEnabled() const {
    return enabled_checkbox_->isChecked();
}

QString WordSelectPage::sourceLanguage() const {
    return modelNameAt(source_lang_combo_);
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
