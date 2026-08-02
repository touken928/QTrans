#include "ui/pages/models/model_page.h"
#include "ui/pages/models/model_row.h"
#include "ui/shared/theme/theme.h"
#include "shared/string_bridge.h"
#include "domain/inference/inference_resolver.h"
#include "domain/model-catalog/model_catalog.h"
#include "domain/inference/runtime_capabilities.h"
#include "domain/download/download.h"

#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyle>
#include <QVBoxLayout>

namespace {

// Refreshes a dynamic-property selector after the property changed, so the
// stylesheet retints the widget immediately.
void repolish(QWidget *widget) {
    if (widget != nullptr && widget->style() != nullptr) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
    }
}

}  // namespace

ModelPage::ModelPage(QWidget *parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("page"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(Theme::Space::xxl, Theme::Space::xxl,
                             Theme::Space::xxl, Theme::Space::xxl);
    root->setSpacing(Theme::Space::lg);

    // ── Configured-model summary (stable projection) ────────────────────
    auto *summary_card = new QFrame(this);
    summary_card->setObjectName(QStringLiteral("settingsSection"));

    auto *summary_layout = new QVBoxLayout(summary_card);
    summary_layout->setContentsMargins(Theme::Space::xl, Theme::Space::lg,
                                       Theme::Space::xl, Theme::Space::lg);
    summary_layout->setSpacing(Theme::Space::xs);

    auto *summary_caption = new QLabel(QStringLiteral("Configured Model"),
                                       summary_card);
    summary_caption->setObjectName(QStringLiteral("toolbarCaption"));
    summary_layout->addWidget(summary_caption);

    auto *name_row = new QHBoxLayout();
    name_row->setSpacing(Theme::Space::sm);

    summary_name_ = new QLabel(QStringLiteral("No model selected"), summary_card);
    summary_name_->setObjectName(QStringLiteral("modelSummaryName"));
    name_row->addWidget(summary_name_);

    summary_status_dot_ = new QLabel(summary_card);
    summary_status_dot_->setObjectName(QStringLiteral("modelStatusDot"));
    summary_status_dot_->setFixedSize(Theme::Size::statusDot, Theme::Size::statusDot);
    name_row->addWidget(summary_status_dot_);

    summary_status_ = new QLabel(QStringLiteral("Not downloaded"), summary_card);
    summary_status_->setObjectName(QStringLiteral("modelSummaryStatus"));
    name_row->addWidget(summary_status_);

    name_row->addStretch(1);
    summary_layout->addLayout(name_row);

    summary_path_ = new QLabel(summary_card);
    summary_path_->setObjectName(QStringLiteral("mutedLabel"));
    summary_path_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary_path_->setWordWrap(false);
    summary_layout->addWidget(summary_path_);

    root->addWidget(summary_card);

    // ── Directory picker row ────────────────────────────────────────────
    auto *dir_card = new QFrame(this);
    dir_card->setObjectName(QStringLiteral("settingsSection"));

    auto *dir_section = new QVBoxLayout(dir_card);
    dir_section->setContentsMargins(Theme::Space::xl, Theme::Space::lg,
                                    Theme::Space::xl, Theme::Space::lg);
    dir_section->setSpacing(Theme::Space::md);

    auto *dir_title = new QLabel(QStringLiteral("Model Storage"), dir_card);
    dir_title->setObjectName(QStringLiteral("sectionTitle"));
    dir_section->addWidget(dir_title);

    auto *dir_row = new QHBoxLayout();
    dir_row->setSpacing(Theme::Space::sm);

    dir_edit_ = new QLineEdit(dir_card);
    dir_edit_->setReadOnly(true);
    dir_edit_->setPlaceholderText(QStringLiteral("Select a folder to store model files..."));
    dir_row->addWidget(dir_edit_, 1);

    browse_btn_ = new QPushButton(QStringLiteral("Browse\u2026"), dir_card);
    browse_btn_->setCursor(Qt::PointingHandCursor);
    browse_btn_->setToolTip(QStringLiteral("Choose where model files are stored"));
    browse_btn_->setAccessibleName(QStringLiteral("Browse model storage folder"));
    dir_row->addWidget(browse_btn_);

    auto *hint = new QLabel(
        QStringLiteral("Model files are downloaded from HuggingFace or ModelSpec "
                       "to this directory. The folder cannot change while a model "
                       "is loading or downloading."),
        dir_card);
    hint->setObjectName(QStringLiteral("mutedLabel"));
    hint->setWordWrap(true);

    dir_section->addLayout(dir_row);
    dir_section->addWidget(hint);
    root->addWidget(dir_card);

    // ── Model rows area (scrollable) ────────────────────────────────────
    auto *models_title = new QLabel(QStringLiteral("Model Library"), this);
    models_title->setObjectName(QStringLiteral("sectionTitle"));
    root->addWidget(models_title);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->viewport()->setAutoFillBackground(false);

    rows_container_ = new QWidget(scroll_);
    rows_container_->setObjectName(QStringLiteral("modelRowsContainer"));
    rows_layout_ = new QVBoxLayout(rows_container_);
    rows_layout_->setContentsMargins(0, 0, 0, 0);
    rows_layout_->setSpacing(Theme::Space::sm);
    rows_layout_->addStretch(1);

    scroll_->setWidget(rows_container_);
    root->addWidget(scroll_, 1);

    // ── Connections ─────────────────────────────────────────────────────
    connect(browse_btn_, &QPushButton::clicked, this, &ModelPage::chooseModelsDir);
}

// ── Public API ──────────────────────────────────────────────────────────────

void ModelPage::setSettings(const AppPaths &paths, const AppSettings &settings) {
    paths_ = paths;
    settings_ = settings;

    const QSignalBlocker block(dir_edit_);
    dir_edit_->setText(qtrans::app::from_utf8(settings_.effectiveModelsDir(paths_)));

    const auto *entry = settings_.selectedModel();
    configured_model_id_ = entry != nullptr ? qtrans::app::from_utf8(entry->id)
                                            : QString{};

    syncRows();
    refreshRowStates();
    refreshSummary();
}

void ModelPage::setRuntimeCapabilities(const RuntimeCapabilities &caps) {
    has_runtime_caps_ = true;
    runtime_caps_ = &caps;
    refreshRowStates();
    refreshSummary();
}

void ModelPage::setLoadedModelId(const QString &model_id) {
    loaded_model_id_ = model_id;
    refreshRowStates();
    refreshSummary();
}

void ModelPage::setModelLoaded(bool loaded) {
    model_loaded_ = loaded;
    if (!loaded) {
        loaded_model_id_.clear();
    }
    refreshRowStates();
    refreshSummary();
}

void ModelPage::setLoadingModelId(const QString &model_id) {
    loading_model_id_ = model_id;
    refreshRowStates();
    refreshSummary();
}

void ModelPage::setUnloading(bool unloading) {
    unloading_ = unloading;
    refreshRowStates();
    refreshSummary();
}

void ModelPage::setDownloadingModelId(const QString &model_id) {
    downloading_model_id_ = model_id;
    refreshRowStates();
    refreshSummary();
}

void ModelPage::setDownloadProgress(qint64 downloaded, qint64 total) {
    download_done_ = downloaded;
    download_total_ = total;
    refreshRowStates();
    refreshSummary();
}

void ModelPage::setInferenceActive(bool active) {
    inference_active_ = active;
    refreshRowStates();
}

void ModelPage::applyTo(AppSettings &settings) const {
    const QString dir = dir_edit_->text().trimmed();
    settings.setEffectiveModelsDir(paths_, qtrans::app::to_utf8(dir));
    settings.model_id = settings_.model_id;
}

// ── Internal helpers ────────────────────────────────────────────────────────

void ModelPage::syncRows() {
    // Remove rows whose catalog entry no longer exists (defensive; the
    // catalog is static today).
    for (auto it = row_order_.begin(); it != row_order_.end();) {
        if (find_model_by_id(qtrans::app::to_utf8(*it)) == nullptr) {
            ModelRow *row = rows_.take(*it);
            if (row != nullptr) {
                rows_layout_->removeWidget(row);
                row->deleteLater();
            }
            it = row_order_.erase(it);
        } else {
            ++it;
        }
    }

    // Add missing rows in catalog order. Existing rows are kept untouched so
    // the configured highlight, scroll position and focus survive refreshes.
    for (const ModelCatalogEntry &entry : model_catalog()) {
        const QString model_id = qtrans::app::from_utf8(entry.id);
        if (rows_.contains(model_id)) {
            continue;
        }
        auto *row = new ModelRow(
            model_id, qtrans::app::from_utf8(entry.display_name),
            qtrans::app::from_utf8(entry.filename), rows_container_);

        const QString captured_id = model_id;
        connect(row, &ModelRow::loadClicked, this, [this, captured_id]() {
            settings_.setSelectedModelId(qtrans::app::to_utf8(captured_id));
            emit loadModelRequested(captured_id);
        });
        connect(row, &ModelRow::downloadClicked, this, [this, captured_id]() {
            settings_.setSelectedModelId(qtrans::app::to_utf8(captured_id));
            emit downloadModelRequested(captured_id);
        });
        connect(row, &ModelRow::unloadClicked, this, [this, captured_id]() {
            emit unloadModelRequested(captured_id);
        });
        connect(row, &ModelRow::deleteClicked, this, [this, captured_id]() {
            emit deleteModelRequested(captured_id);
        });
        connect(row, &ModelRow::cancelDownloadClicked, this, [this](const QString &) {
            emit cancelDownloadRequested();
        });

        rows_layout_->insertWidget(rows_layout_->count() - 1, row);
        rows_.insert(model_id, row);
        row_order_.append(model_id);
    }
}

void ModelPage::refreshRowStates() {
    // Only one operation is allowed at a time; while any lifecycle work
    // runs, every row gates its actions (the busy row additionally carries
    // the activity presentation).
    const bool any_operation =
        !loading_model_id_.isEmpty() || unloading_ || !downloading_model_id_.isEmpty();

    for (const QString &model_id : row_order_) {
        ModelRow *row = rows_.value(model_id, nullptr);
        if (row == nullptr) {
            continue;
        }

        const bool is_configured = model_id == configured_model_id_;
        const bool is_loaded = model_loaded_ && model_id == loaded_model_id_;
        const bool file_exists = modelFileExists(model_id);
        const bool available = modelIsAvailable(model_id);

        row->setConfigured(is_configured);
        row->setFilePresent(file_exists);
        row->setLibraryBusy(any_operation);
        row->setInferenceBusy(inference_active_);

        ModelRow::Status status;
        QString detail;
        if (is_loaded) {
            status = ModelRow::Status::Loaded;
        } else if (!available) {
            status = ModelRow::Status::Unavailable;
            if (has_runtime_caps_ && runtime_caps_ != nullptr) {
                const auto *entry = find_model_by_id(qtrans::app::to_utf8(model_id));
                if (entry != nullptr) {
                    detail = qtrans::app::from_utf8(unavailable_reason(*entry, *runtime_caps_));
                }
            }
        } else if (file_exists) {
            status = ModelRow::Status::Ready;
        } else {
            status = ModelRow::Status::NotDownloaded;
        }
        row->setStatus(status, detail);

        // Per-row lifecycle: exactly one operation can be active, and it is
        // always correlated to the row it belongs to.
        if (model_id == loading_model_id_) {
            row->setActivity(ModelRow::Activity::Loading);
        } else if (model_id == downloading_model_id_) {
            row->setActivity(ModelRow::Activity::Downloading);
            row->setDownloadProgress(download_done_, download_total_);
        } else if (unloading_ && is_loaded) {
            row->setActivity(ModelRow::Activity::Unloading);
        } else {
            row->setActivity(ModelRow::Activity::Idle);
        }
    }
}

void ModelPage::refreshSummary() {
    const auto *entry = settings_.selectedModel();
    if (entry == nullptr) {
        summary_name_->setText(QStringLiteral("No model selected"));
        summary_status_->setText(QString());
        summary_path_->setText(QString());
        summary_status_dot_->setProperty("dotState", QStringLiteral("muted"));
        repolish(summary_status_dot_);
        return;
    }

    configured_model_id_ = qtrans::app::from_utf8(entry->id);
    const QString display_name = qtrans::app::from_utf8(entry->display_name);
    const QString model_path = qtrans::app::from_utf8(
        (std::filesystem::path(settings_.effectiveModelsDir(paths_)) / entry->filename)
            .string());

    summary_name_->setText(display_name);
    summary_path_->setText(model_path);

    const bool file_exists = download_file_exists(qtrans::app::to_utf8(model_path));
    const bool available = modelIsAvailable(configured_model_id_);

    QString status_text;
    QString dot_state = QStringLiteral("muted");
    if (configured_model_id_ == loading_model_id_) {
        status_text = QStringLiteral("Loading\u2026");
        dot_state = QStringLiteral("primary");
    } else if (configured_model_id_ == downloading_model_id_) {
        status_text = QStringLiteral("Downloading\u2026");
        dot_state = QStringLiteral("primary");
    } else if (unloading_ && model_loaded_ && configured_model_id_ == loaded_model_id_) {
        // Unloading must be projected before the still-truthful "loaded"
        // state: the model is technically in memory, but the lifecycle step
        // in flight is what the user needs to see.
        status_text = QStringLiteral("Unloading\u2026");
        dot_state = QStringLiteral("primary");
    } else if (model_loaded_ && configured_model_id_ == loaded_model_id_) {
        status_text = QStringLiteral("Loaded");
        dot_state = QStringLiteral("success");
    } else if (!available) {
        if (has_runtime_caps_ && runtime_caps_ != nullptr) {
            status_text = qtrans::app::from_utf8(unavailable_reason(*entry, *runtime_caps_));
        } else {
            status_text = QStringLiteral("Unavailable on this system");
        }
        dot_state = QStringLiteral("warning");
    } else if (file_exists) {
        status_text = QStringLiteral("Downloaded \u2014 not loaded");
        dot_state = QStringLiteral("success");
    } else {
        status_text = QStringLiteral("Not downloaded");
        dot_state = QStringLiteral("muted");
    }
    summary_status_->setText(status_text);
    summary_status_dot_->setProperty("dotState", dot_state);
    repolish(summary_status_dot_);

    // The directory cannot change while any lifecycle operation runs: the
    // file being loaded or downloaded must not be displaced underneath.
    const bool any_operation =
        !loading_model_id_.isEmpty() || unloading_ || !downloading_model_id_.isEmpty();
    browse_btn_->setEnabled(!any_operation);
}

void ModelPage::chooseModelsDir() {
    const QString current = dir_edit_->text();
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Models Directory"),
        current.isEmpty() ? QString::fromStdString(paths_.models_dir.string()) : current,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (dir.isEmpty()) {
        return;
    }

    dir_edit_->setText(dir);
    settings_.setEffectiveModelsDir(paths_, qtrans::app::to_utf8(dir));
    refreshRowStates();
    refreshSummary();
    emit modelEdited();
}

QString ModelPage::modelFilePath(const QString &model_id) const {
    const auto *entry = find_model_by_id(qtrans::app::to_utf8(model_id));
    if (entry == nullptr) {
        return {};
    }
    const std::string dir = settings_.effectiveModelsDir(paths_);
    return qtrans::app::from_utf8(
        (std::filesystem::path(dir) / entry->filename).string());
}

bool ModelPage::modelFileExists(const QString &model_id) const {
    const QString path = modelFilePath(model_id);
    return !path.isEmpty() && download_file_exists(qtrans::app::to_utf8(path));
}

bool ModelPage::modelIsAvailable(const QString &model_id) const {
    if (!has_runtime_caps_ || runtime_caps_ == nullptr) {
        return true;
    }
    const auto *entry = find_model_by_id(qtrans::app::to_utf8(model_id));
    if (entry == nullptr) {
        return false;
    }
    return static_cast<bool>(resolve_inference(*entry, *runtime_caps_));
}
