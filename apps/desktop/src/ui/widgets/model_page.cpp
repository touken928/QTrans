#include "ui/widgets/model_page.h"

#include "ui/string_bridge.h"
#include "ui/widget_utils.h"
#include "model/inference_resolver.h"
#include "model/model_catalog.h"
#include "model/runtime_capabilities.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardItemModel>
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
            qtrans::app::from_utf8(entry.display_name),
            qtrans::app::from_utf8(entry.id));
    }
    configureComboBox(model_combo_, 260);

    delete_button_ = new QPushButton(QStringLiteral("Delete"), this);
    delete_button_->setObjectName(QStringLiteral("deleteModelBtn"));

    auto *model_row = new QHBoxLayout();
    model_row->setContentsMargins(0, 0, 0, 0);
    model_row->setSpacing(8);
    model_row->addWidget(model_combo_, 1);
    model_row->addWidget(delete_button_);
    model_form->addRow(QStringLiteral("Model"), model_row);

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
    connect(delete_button_, &QPushButton::clicked, this, &ModelPage::deleteModelRequested);
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

    const int model_index = model_combo_->findData(qtrans::app::from_utf8(settings_.model_id));
    model_combo_->setCurrentIndex(model_index >= 0 ? model_index : 0);
    models_dir_edit_->setText(qtrans::app::from_utf8(settings_.effectiveModelsDir(paths_)));

    updateModelAvailability();
}

void ModelPage::setRuntimeCapabilities(const RuntimeCapabilities &caps) {
    has_runtime_caps_ = true;
    runtime_caps_ = &caps;
    updateModelAvailability();
}

void ModelPage::applyTo(AppSettings &settings) const {
    settings.setSelectedModelId(qtrans::app::to_utf8(model_combo_->currentData().toString()));
    settings.setEffectiveModelsDir(paths_, qtrans::app::to_utf8(models_dir_edit_->text()));
}

void ModelPage::setBusy(bool busy) {
    busy_ = busy;
    updateActions();
}

void ModelPage::setModelLoaded(bool loaded) {
    model_loaded_ = loaded;
    updateActions();
}

void ModelPage::updateModelAvailability() {
    if (!has_runtime_caps_ || runtime_caps_ == nullptr) {
        return;
    }

    const QSignalBlocker block_combo(model_combo_);
    for (int index = 0; index < model_combo_->count(); ++index) {
        const std::string model_id = qtrans::app::to_utf8(model_combo_->itemData(index).toString());
        const ModelCatalogEntry *entry = find_model_by_id(model_id);
        if (entry == nullptr) {
            model_combo_->setItemData(index, 0, Qt::UserRole - 1);
            continue;
        }

        const bool available = static_cast<bool>(resolve_inference(*entry, *runtime_caps_));
        const QStandardItemModel *model = qobject_cast<QStandardItemModel *>(model_combo_->model());
        if (model != nullptr) {
            QStandardItem *item = model->item(index);
            if (item != nullptr) {
                item->setEnabled(available);
            }
        }
        if (!available) {
            const QString reason = qtrans::app::from_utf8(unavailable_reason(*entry, *runtime_caps_));
            model_combo_->setItemData(index, reason, Qt::ToolTipRole);
        } else {
            model_combo_->setItemData(index, QVariant{}, Qt::ToolTipRole);
        }
    }

    const int current_index = model_combo_->currentIndex();
    if (current_index >= 0) {
        const QStandardItemModel *model = qobject_cast<QStandardItemModel *>(model_combo_->model());
        if (model != nullptr) {
            const QStandardItem *item = model->item(current_index);
            if (item != nullptr && !item->isEnabled()) {
                for (int index = 0; index < model_combo_->count(); ++index) {
                    const QStandardItem *candidate = model->item(index);
                    if (candidate != nullptr && candidate->isEnabled()) {
                        model_combo_->setCurrentIndex(index);
                        applyTo(settings_);
                        break;
                    }
                }
            }
        }
    }
}

void ModelPage::updateActions() {
    const bool idle = !busy_;
    model_combo_->setEnabled(idle);
    models_dir_edit_->setEnabled(idle);
    save_button_->setEnabled(idle);
    load_button_->setEnabled(idle && !model_loaded_);
    unload_button_->setEnabled(idle && model_loaded_);
    delete_button_->setEnabled(idle && !model_loaded_);
}
