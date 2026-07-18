#include "ui/pages/batch/batch_lang_panel.h"
#include "ui/shared/theme/theme.h"
#include "ui/shared/widget_utils.h"
#include "domain/model-catalog/language_list.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

BatchLangPanel::BatchLangPanel(QWidget *parent)
    : QWidget(parent) {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(Theme::Space::lg, Theme::Space::lg,
                             Theme::Space::lg, Theme::Space::lg);
    root->setSpacing(Theme::Space::md);

    auto *title = new QLabel(QStringLiteral("Translate From → To"), this);
    title->setObjectName(QStringLiteral("titleLabel"));
    root->addWidget(title);

    auto *body = new QLabel(
        QStringLiteral("Choose the source and target language, then select one or more files to translate."),
        this);
    body->setWordWrap(true);
    root->addWidget(body);

    constexpr int kLabelWidth = 110;

    auto *source_row = new QHBoxLayout();
    auto *source_label = new QLabel(QStringLiteral("Source language"), this);
    source_label->setStyleSheet(QStringLiteral("font-size: %1px;").arg(Theme::Font::md));
    source_label->setMinimumWidth(kLabelWidth);
    source_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    source_row->addWidget(source_label);

    source_combo_ = new QComboBox(this);
    for (int i = 0; i < translation_language_count(); ++i) {
        source_combo_->addItem(
            QString::fromUtf8(translation_languages()[i].label));
    }
    configureComboBox(source_combo_, 140);
    source_row->addWidget(source_combo_, 1);
    root->addLayout(source_row);

    auto *target_row = new QHBoxLayout();
    auto *target_label = new QLabel(QStringLiteral("Target language"), this);
    target_label->setStyleSheet(QStringLiteral("font-size: %1px;").arg(Theme::Font::md));
    target_label->setMinimumWidth(kLabelWidth);
    target_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    target_row->addWidget(target_label);

    target_combo_ = new QComboBox(this);
    for (int i = 0; i < translation_language_count(); ++i) {
        target_combo_->addItem(
            QString::fromUtf8(translation_languages()[i].label));
    }
    configureComboBox(target_combo_, 140);
    target_row->addWidget(target_combo_, 1);
    root->addLayout(target_row);

    root->addStretch(1);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);

    auto *select_btn = new QPushButton(QStringLiteral("Select Files…"), this);
    select_btn->setObjectName(QStringLiteral("primaryButton"));
    select_btn->setCursor(Qt::PointingHandCursor);
    select_btn->setMinimumWidth(120);
    buttons->addWidget(select_btn);

    auto *cancel_btn = new QPushButton(QStringLiteral("Cancel"), this);
    cancel_btn->setCursor(Qt::PointingHandCursor);
    cancel_btn->setMinimumWidth(80);
    buttons->addWidget(cancel_btn);
    root->addLayout(buttons);

    connect(select_btn, &QPushButton::clicked, this, &BatchLangPanel::onConfirm);
    connect(cancel_btn, &QPushButton::clicked, this, &BatchLangPanel::onCancel);
}

void BatchLangPanel::setDefaultLanguages(const QString &source_lang,
                                         const QString &target_lang) {
    const int source_index = findLanguageIndex(source_lang);
    const int target_index = findLanguageIndex(target_lang);
    source_combo_->setCurrentIndex(source_index >= 0 ? source_index : 0);
    target_combo_->setCurrentIndex(target_index >= 0 ? target_index : 0);
}

void BatchLangPanel::onConfirm() {
    const QString src = source_combo_->currentText();
    const QString tgt = target_combo_->currentText();
    emit confirmed(src, tgt);
}

void BatchLangPanel::onCancel() {
    emit cancelled();
}

int BatchLangPanel::findLanguageIndex(const QString &label) const {
    return source_combo_->findText(label);
}
