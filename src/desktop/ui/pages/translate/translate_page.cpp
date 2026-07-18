#include "ui/pages/translate/translate_page.h"
#include "ui/shared/theme/theme.h"
#include "shared/string_bridge.h"
#include "ui/shared/widget_utils.h"
#include "domain/logging/component.h"
#include "domain/logging/logger.h"
#include "domain/model-catalog/language_list.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {

int defaultLanguageIndex(const char *id) {
    for (int i = 0; i < translation_language_count(); ++i) {
        if (qtrans::app::from_utf8(translation_languages()[i].id) == qtrans::app::from_utf8(id)) {
            return i;
        }
    }
    return 0;
}

QString languageNameAt(QComboBox *combo) {
    const int index = combo->currentIndex();
    if (index < 0 || index >= translation_language_count()) {
        return QStringLiteral("English");
    }
    return qtrans::app::from_utf8(translation_languages()[index].model_name);
}

}  // namespace

TranslatePage::TranslatePage(QWidget *parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("page"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(Theme::Space::xxl, Theme::Space::xxl,
                             Theme::Space::xxl, Theme::Space::xxl);
    root->setSpacing(Theme::Space::lg);

    // ── Toolbar (transparent, no card wrapper) ────────────────────────
    auto *toolbar_layout = new QHBoxLayout();
    toolbar_layout->setContentsMargins(0, 0, 0, 0);
    toolbar_layout->setSpacing(Theme::Space::sm);

    source_lang_combo_ = new QComboBox(this);
    for (int i = 0; i < translation_language_count(); ++i) {
        source_lang_combo_->addItem(qtrans::app::from_utf8(translation_languages()[i].label));
    }
    source_lang_combo_->setCurrentIndex(defaultLanguageIndex("en"));
    configureComboBox(source_lang_combo_, 140);
    toolbar_layout->addWidget(source_lang_combo_);

    swap_button_ = new QPushButton(QStringLiteral("Swap"), this);
    swap_button_->setCursor(Qt::PointingHandCursor);
    swap_button_->setToolTip(QStringLiteral("Swap source and target languages"));
    toolbar_layout->addWidget(swap_button_);

    target_lang_combo_ = new QComboBox(this);
    for (int i = 0; i < translation_language_count(); ++i) {
        target_lang_combo_->addItem(qtrans::app::from_utf8(translation_languages()[i].label));
    }
    target_lang_combo_->setCurrentIndex(defaultLanguageIndex("zh"));
    configureComboBox(target_lang_combo_, 140);
    toolbar_layout->addWidget(target_lang_combo_);

    translate_button_ = new QPushButton(QStringLiteral("Translate"), this);
    translate_button_->setObjectName(QStringLiteral("translateButton"));
    translate_button_->setCursor(Qt::PointingHandCursor);
    {
        QPushButton probe(QStringLiteral("Stop"), this);
        probe.setObjectName(translate_button_->objectName());
        probe.setFont(translate_button_->font());
        const int w = qMax(translate_button_->sizeHint().width(),
                           probe.sizeHint().width()) +
                      16;
        translate_button_->setFixedWidth(w);
    }
    toolbar_layout->addWidget(translate_button_);
    toolbar_layout->addStretch(1);

    root->addLayout(toolbar_layout);

    // ── Splitter panels ───────────────────────────────────────────────
    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->setHandleWidth(1);
    splitter_->setChildrenCollapsible(false);

    // Source panel
    auto *source_panel = new QWidget(splitter_);
    source_panel->setMinimumWidth(Theme::Size::minPanelWidth);
    auto *src_layout = new QVBoxLayout(source_panel);
    src_layout->setContentsMargins(0, 0, 0, 0);
    src_layout->setSpacing(Theme::Space::sm);

    auto *src_header = new QLabel(QStringLiteral("Source"), source_panel);
    src_header->setObjectName(QStringLiteral("panelLabel"));
    src_layout->addWidget(src_header);

    source_edit_ = new QPlainTextEdit(source_panel);
    source_edit_->setPlaceholderText(QStringLiteral("Enter text to translate..."));
    source_edit_->setTabChangesFocus(true);
    src_layout->addWidget(source_edit_, 1);

    // Target panel
    auto *target_panel = new QWidget(splitter_);
    target_panel->setMinimumWidth(Theme::Size::minPanelWidth);
    auto *tgt_layout = new QVBoxLayout(target_panel);
    tgt_layout->setContentsMargins(0, 0, 0, 0);
    tgt_layout->setSpacing(Theme::Space::sm);

    auto *tgt_header = new QLabel(QStringLiteral("Target"), target_panel);
    tgt_header->setObjectName(QStringLiteral("panelLabel"));
    tgt_layout->addWidget(tgt_header);

    target_edit_ = new QPlainTextEdit(target_panel);
    target_edit_->setReadOnly(true);
    target_edit_->setTabChangesFocus(true);
    tgt_layout->addWidget(target_edit_, 1);

    // Back-translate panel
    back_panel_ = new QWidget(splitter_);
    back_panel_->setMinimumWidth(Theme::Size::minPanelWidth);
    auto *back_layout = new QVBoxLayout(back_panel_);
    back_layout->setContentsMargins(0, 0, 0, 0);
    back_layout->setSpacing(Theme::Space::sm);

    auto *back_header = new QLabel(QStringLiteral("Back-translate"), back_panel_);
    back_header->setObjectName(QStringLiteral("panelLabel"));
    back_layout->addWidget(back_header);

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

    // ── Footer ────────────────────────────────────────────────────────
    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("pageFooter"));

    auto *footer_layout = new QHBoxLayout(footer);
    footer_layout->setContentsMargins(0, 0, 0, 0);
    footer_layout->setSpacing(Theme::Space::md);

    status_label_ = new QLabel(QStringLiteral("Ready"), footer);
    status_label_->setObjectName(QStringLiteral("statusLabel"));
    footer_layout->addWidget(status_label_, 1);

    clear_button_ = new QPushButton(QStringLiteral("Clear"), footer);
    copy_button_ = new QPushButton(QStringLiteral("Copy Result"), footer);
    footer_layout->addWidget(clear_button_);
    footer_layout->addWidget(copy_button_);

    back_translate_checkbox_ = new QCheckBox(QStringLiteral("Back-translate"), footer);
    back_translate_checkbox_->setToolTip(
        QStringLiteral("Show a third column and translate the result back to the source language"));
    footer_layout->addWidget(back_translate_checkbox_);

    root->addWidget(footer);

    // ── Connections ───────────────────────────────────────────────────
    connect(translate_button_, &QPushButton::clicked, this, &TranslatePage::onTranslate);
    connect(swap_button_, &QPushButton::clicked, this, &TranslatePage::onSwap);
    connect(clear_button_, &QPushButton::clicked, this, &TranslatePage::onClear);
    connect(copy_button_, &QPushButton::clicked, this, &TranslatePage::onCopyResult);
    connect(back_translate_checkbox_, &QCheckBox::toggled,
            this, &TranslatePage::onBackTranslateToggled);
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
    translate_button_->setText(translating ? QStringLiteral("Stop")
                                           : QStringLiteral("Translate"));
    source_edit_->setReadOnly(busy_ || translating_);
    updateActions();
}

void TranslatePage::setModelLoaded(bool loaded) {
    model_loaded_ = loaded;
    updateActions();
}

void TranslatePage::setStatus(const QString &status) {
    status_label_->setText(status);
}

void TranslatePage::resetTarget() {
    target_edit_->clear();
}

void TranslatePage::resetBackTranslate() {
    back_edit_->clear();
}

void TranslatePage::appendTarget(const QString &piece) {
    qtrans::log::get(qtrans::log::Component::App)
        ->debug("appendTarget len:{}", piece.size());
    target_edit_->moveCursor(QTextCursor::End);
    target_edit_->insertPlainText(piece);
    target_edit_->moveCursor(QTextCursor::End);
    target_edit_->ensureCursorVisible();
}

void TranslatePage::appendBackTranslate(const QString &piece) {
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

void TranslatePage::setSourceLanguage(const QString &model_name) {
    const int idx = source_lang_combo_->findText(model_name);
    if (idx >= 0) {
        source_lang_combo_->setCurrentIndex(idx);
    }
}

void TranslatePage::setTargetLanguage(const QString &model_name) {
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
