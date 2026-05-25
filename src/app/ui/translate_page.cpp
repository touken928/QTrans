#include "app/ui/translate_page.h"

#include "app/widget_utils.h"
#include "translation/translation_languages.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDebug>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QString>
#include <QTextCursor>
#include <QThread>
#include <QVBoxLayout>

namespace {

int defaultLanguageIndex(const char * id) {
    for (int i = 0; i < translation_language_count(); ++i) {
        if (QString::fromUtf8(translation_languages()[i].id) == QString::fromUtf8(id)) {
            return i;
        }
    }
    return 0;
}

QString languageNameAt(QComboBox * combo) {
    const int index = combo->currentIndex();
    if (index < 0 || index >= translation_language_count()) {
        return QStringLiteral("English");
    }
    return QString::fromUtf8(translation_languages()[index].model_name);
}

} // namespace

TranslatePage::TranslatePage(QWidget * parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("page"));

    auto * root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(10);

    auto * toolbar = new QHBoxLayout();
    source_lang_combo_ = new QComboBox(this);
    for (int i = 0; i < translation_language_count(); ++i) {
        source_lang_combo_->addItem(QString::fromUtf8(translation_languages()[i].label));
    }
    source_lang_combo_->setCurrentIndex(defaultLanguageIndex("en"));
    configureComboBox(source_lang_combo_, 140);
    toolbar->addWidget(source_lang_combo_);

    swap_button_ = new QPushButton(QStringLiteral("Swap"), this);
    toolbar->addWidget(swap_button_);

    target_lang_combo_ = new QComboBox(this);
    for (int i = 0; i < translation_language_count(); ++i) {
        target_lang_combo_->addItem(QString::fromUtf8(translation_languages()[i].label));
    }
    target_lang_combo_->setCurrentIndex(defaultLanguageIndex("zh"));
    configureComboBox(target_lang_combo_, 140);
    toolbar->addWidget(target_lang_combo_);

    translate_button_ = new QPushButton(QStringLiteral("Translate"), this);
    translate_button_->setObjectName(QStringLiteral("translateButton"));
    {
        QPushButton width_probe(QStringLiteral("Stop"), this);
        width_probe.setObjectName(translate_button_->objectName());
        width_probe.setFont(translate_button_->font());
        const int action_width = qMax(
            translate_button_->sizeHint().width(),
            width_probe.sizeHint().width())
            + 16;
        translate_button_->setFixedWidth(action_width);
    }
    toolbar->addWidget(translate_button_);
    toolbar->addStretch(1);
    root->addLayout(toolbar);

    splitter_ = new QSplitter(Qt::Horizontal, this);

    auto * source_panel = new QWidget(splitter_);
    auto * source_layout = new QVBoxLayout(source_panel);
    source_layout->setContentsMargins(0, 0, 0, 0);
    auto * source_label = new QLabel(QStringLiteral("Source"), source_panel);
    source_label->setObjectName(QStringLiteral("panelLabel"));
    source_layout->addWidget(source_label);
    source_edit_ = new QPlainTextEdit(source_panel);
    source_edit_->setPlaceholderText(QStringLiteral("Enter text to translate..."));
    source_layout->addWidget(source_edit_, 1);

    auto * target_panel = new QWidget(splitter_);
    auto * target_layout = new QVBoxLayout(target_panel);
    target_layout->setContentsMargins(0, 0, 0, 0);
    auto * target_label = new QLabel(QStringLiteral("Target"), target_panel);
    target_label->setObjectName(QStringLiteral("panelLabel"));
    target_layout->addWidget(target_label);
    target_edit_ = new QPlainTextEdit(target_panel);
    target_edit_->setReadOnly(true);
    target_layout->addWidget(target_edit_, 1);

    back_panel_ = new QWidget(splitter_);
    auto * back_layout = new QVBoxLayout(back_panel_);
    back_layout->setContentsMargins(0, 0, 0, 0);
    auto * back_label = new QLabel(QStringLiteral("Back-translate"), back_panel_);
    back_label->setObjectName(QStringLiteral("panelLabel"));
    back_layout->addWidget(back_label);
    back_edit_ = new QPlainTextEdit(back_panel_);
    back_edit_->setReadOnly(true);
    back_layout->addWidget(back_edit_, 1);

    splitter_->addWidget(source_panel);
    splitter_->addWidget(target_panel);
    splitter_->addWidget(back_panel_);
    splitter_->setStretchFactor(0, 1);
    splitter_->setStretchFactor(1, 1);
    splitter_->setStretchFactor(2, 1);
    root->addWidget(splitter_, 1);

    auto * footer = new QHBoxLayout();
    status_label_ = new QLabel(QStringLiteral("Ready"), this);
    status_label_->setObjectName(QStringLiteral("statusLabel"));
    footer->addWidget(status_label_, 1);

    clear_button_ = new QPushButton(QStringLiteral("Clear"), this);
    copy_button_ = new QPushButton(QStringLiteral("Copy Result"), this);
    footer->addWidget(clear_button_);
    footer->addWidget(copy_button_);

    back_translate_checkbox_ = new QCheckBox(QStringLiteral("Back-translate"), this);
    back_translate_checkbox_->setToolTip(
        QStringLiteral("Show a third column and translate the result back to the source language"));
    footer->addWidget(back_translate_checkbox_);

    root->addLayout(footer);

    connect(translate_button_, &QPushButton::clicked, this, &TranslatePage::onTranslate);
    connect(swap_button_, &QPushButton::clicked, this, &TranslatePage::onSwap);
    connect(clear_button_, &QPushButton::clicked, this, &TranslatePage::onClear);
    connect(copy_button_, &QPushButton::clicked, this, &TranslatePage::onCopyResult);
    connect(back_translate_checkbox_, &QCheckBox::toggled, this, &TranslatePage::onBackTranslateToggled);
    connect(source_lang_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TranslatePage::languageChanged);
    connect(target_lang_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TranslatePage::languageChanged);

    setBackTranslateVisible(false);
    updateActions();
}

void TranslatePage::setBusy(bool busy) {
    busy_ = busy;
    source_edit_->setReadOnly(busy || translating_);
    updateActions();
}

void TranslatePage::setTranslating(bool translating) {
    translating_ = translating;
    translate_button_->setText(translating ? QStringLiteral("Stop") : QStringLiteral("Translate"));
    source_edit_->setReadOnly(busy_ || translating_);
    updateActions();
}

void TranslatePage::setModelLoaded(bool loaded) {
    model_loaded_ = loaded;
    updateActions();
}

void TranslatePage::setStatus(const QString & status) {
    status_label_->setText(status);
}

void TranslatePage::resetTarget() {
    target_edit_->clear();
}

void TranslatePage::resetBackTranslate() {
    back_edit_->clear();
}

void TranslatePage::appendTarget(const QString & piece) {
    qDebug() << "[TranslatePage] appendTarget len:" << piece.size() << "thread:" << QThread::currentThread();
    target_edit_->moveCursor(QTextCursor::End);
    target_edit_->insertPlainText(piece);
    target_edit_->moveCursor(QTextCursor::End);
    target_edit_->ensureCursorVisible();
}

void TranslatePage::appendBackTranslate(const QString & piece) {
    back_edit_->moveCursor(QTextCursor::End);
    back_edit_->insertPlainText(piece);
    back_edit_->moveCursor(QTextCursor::End);
    back_edit_->ensureCursorVisible();
}

QString TranslatePage::targetText() const {
    return target_edit_->toPlainText();
}

void TranslatePage::prepareForTranslation(bool back_translate) {
    resetTarget();
    resetBackTranslate();
    setBackTranslateVisible(back_translate);
}

void TranslatePage::onBackTranslateToggled(bool enabled) {
    setBackTranslateVisible(enabled);
    if (!enabled) {
        resetBackTranslate();
    }
}

void TranslatePage::setBackTranslateVisible(bool visible) {
    back_panel_->setVisible(visible);
}

void TranslatePage::onTranslate() {
    if (translating_) {
        emit cancelRequested();
        return;
    }

    if (!model_loaded_) {
        setStatus(QStringLiteral("Load a model first"));
        return;
    }
    if (source_edit_->toPlainText().trimmed().isEmpty()) {
        setStatus(QStringLiteral("Enter text to translate"));
        return;
    }

    const bool back_translate = back_translate_checkbox_->isChecked();
    prepareForTranslation(back_translate);

    emit translateRequested(
        source_edit_->toPlainText(),
        targetLanguageName(),
        sourceLanguageName(),
        back_translate);
}

void TranslatePage::onSwap() {
    const int source_index = source_lang_combo_->currentIndex();
    source_lang_combo_->setCurrentIndex(target_lang_combo_->currentIndex());
    target_lang_combo_->setCurrentIndex(source_index);

    const QString source_text = source_edit_->toPlainText();
    source_edit_->setPlainText(target_edit_->toPlainText());
    target_edit_->setPlainText(source_text);

    emit languageChanged();
}

void TranslatePage::onClear() {
    source_edit_->clear();
    target_edit_->clear();
    back_edit_->clear();
}

void TranslatePage::onCopyResult() {
    QApplication::clipboard()->setText(target_edit_->toPlainText());
}

QString TranslatePage::targetLanguageName() const {
    return languageNameAt(target_lang_combo_);
}

QString TranslatePage::sourceLanguageName() const {
    return languageNameAt(source_lang_combo_);
}

void TranslatePage::setSourceLanguage(const QString & model_name) {
    const int idx = source_lang_combo_->findText(model_name);
    if (idx >= 0) {
        source_lang_combo_->setCurrentIndex(idx);
    }
}

void TranslatePage::setTargetLanguage(const QString & model_name) {
    const int idx = target_lang_combo_->findText(model_name);
    if (idx >= 0) {
        target_lang_combo_->setCurrentIndex(idx);
    }
}

void TranslatePage::updateActions() {
    if (translating_) {
        translate_button_->setEnabled(true);
    } else {
        translate_button_->setEnabled(!busy_ && model_loaded_);
    }
    swap_button_->setEnabled(!busy_ && !translating_);
    clear_button_->setEnabled(!busy_ && !translating_);
    copy_button_->setEnabled(!busy_ && !translating_);
    source_lang_combo_->setEnabled(!busy_ && !translating_);
    target_lang_combo_->setEnabled(!busy_ && !translating_);
    back_translate_checkbox_->setEnabled(!busy_ && !translating_);
}
