#include "app/ui/model_page.h"

#include "app/widget_utils.h"
#include "model/model_catalog.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

ModelPage::ModelPage(QWidget *parent)
    : QWidget(parent) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(14);

    auto *model_form = new QFormLayout();
    model_form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    model_form->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);

    model_combo_ = new QComboBox(this);
    for (const ModelCatalogEntry &entry : model_catalog()) {
        model_combo_->addItem(
            QString::fromStdString(entry.display_name),
            QString::fromStdString(entry.id));
    }
    configureComboBox(model_combo_, 320);
    model_form->addRow(QStringLiteral("Model"), model_combo_);

    models_dir_edit_ = new QLineEdit(this);
    models_dir_edit_->setMinimumWidth(320);
    model_form->addRow(QStringLiteral("Models folder"), models_dir_edit_);

    outer->addLayout(model_form);
    outer->addStretch(1);

    auto *buttons = new QHBoxLayout();
    buttons->addStretch(1);

    save_button_ = new QPushButton(QStringLiteral("Save"), this);
    load_button_ = new QPushButton(QStringLiteral("Load"), this);
    load_button_->setObjectName(QStringLiteral("primaryButton"));
    unload_button_ = new QPushButton(QStringLiteral("Unload"), this);

    buttons->addWidget(save_button_);
    buttons->addWidget(load_button_);
    buttons->addWidget(unload_button_);
    outer->addLayout(buttons);

    connect(save_button_, &QPushButton::clicked, this, &ModelPage::saveRequested);
    connect(load_button_, &QPushButton::clicked, this, &ModelPage::loadModelRequested);
    connect(unload_button_, &QPushButton::clicked, this, &ModelPage::unloadModelRequested);
    connect(model_combo_, &QComboBox::currentIndexChanged, this, [this]() {
        applyTo(settings_);
        emit modelEdited();
    });
    connect(models_dir_edit_, &QLineEdit::textEdited, this, [this]() {
        applyTo(settings_);
        emit modelEdited();
    });

    updateActions();
}

void ModelPage::setSettings(const AppPaths &paths, const AppSettings &settings) {
    paths_ = paths;
    settings_ = settings;

    const QSignalBlocker block_combo(model_combo_);
    const QSignalBlocker block_dir(models_dir_edit_);

    const int model_index = model_combo_->findData(QString::fromStdString(settings_.model_id));
    model_combo_->setCurrentIndex(model_index >= 0 ? model_index : 0);
    models_dir_edit_->setText(QString::fromStdString(settings_.effectiveModelsDir(paths_)));
}

void ModelPage::applyTo(AppSettings &settings) const {
    settings.setSelectedModelId(model_combo_->currentData().toString().toStdString());
    settings.setEffectiveModelsDir(paths_, models_dir_edit_->text().toStdString());
}

void ModelPage::setBusy(bool busy) {
    busy_ = busy;
    updateActions();
}

void ModelPage::setModelLoaded(bool loaded) {
    model_loaded_ = loaded;
    updateActions();
}

void ModelPage::updateActions() {
    const bool idle = !busy_;
    model_combo_->setEnabled(idle);
    models_dir_edit_->setEnabled(idle);
    save_button_->setEnabled(idle);
    load_button_->setEnabled(idle && !model_loaded_);
    unload_button_->setEnabled(idle && model_loaded_);
}
