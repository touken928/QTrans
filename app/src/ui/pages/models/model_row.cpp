#include "ui/pages/models/model_row.h"
#include "ui/shared/theme/theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace {

// Refreshes a dynamic-property selector (e.g. [dotState="success"]) after
// the property changed, so the stylesheet retints the widget immediately.
void repolish(QWidget *widget) {
    if (widget != nullptr && widget->style() != nullptr) {
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
    }
}

}  // namespace

ModelRow::ModelRow(const QString &model_id, const QString &display_name,
                   const QString &filename, QWidget *parent)
    : QWidget(parent),
      model_id_(model_id),
      display_name_(display_name) {
    setObjectName(QStringLiteral("modelRow"));

    auto *row_layout = new QHBoxLayout(this);
    row_layout->setContentsMargins(Theme::Space::lg, Theme::Space::sm,
                                   Theme::Space::lg, Theme::Space::sm);
    row_layout->setSpacing(Theme::Space::lg);

    // ── Left: identity column ───────────────────────────────────────────
    auto *info_col = new QVBoxLayout();
    info_col->setSpacing(Theme::Space::xs);

    auto *name_row = new QHBoxLayout();
    name_row->setSpacing(Theme::Space::sm);

    name_label_ = new QLabel(display_name, this);
    name_label_->setObjectName(QStringLiteral("modelRowName"));
    name_row->addWidget(name_label_);

    configured_badge_ = new QLabel(QStringLiteral("Configured"), this);
    configured_badge_->setObjectName(QStringLiteral("configuredBadge"));
    configured_badge_->setVisible(false);
    name_row->addWidget(configured_badge_);

    name_row->addStretch(1);
    info_col->addLayout(name_row);

    file_label_ = new QLabel(filename, this);
    file_label_->setObjectName(QStringLiteral("modelRowFile"));
    file_label_->setToolTip(filename);
    info_col->addWidget(file_label_);

    row_layout->addLayout(info_col, 1);

    // ── Right: status + actions ─────────────────────────────────────────
    auto *actions_col = new QVBoxLayout();
    actions_col->setSpacing(Theme::Space::xs);

    auto *status_row = new QHBoxLayout();
    status_row->setSpacing(Theme::Space::sm);

    status_dot_ = new QLabel(this);
    status_dot_->setObjectName(QStringLiteral("modelStatusDot"));
    status_dot_->setFixedSize(Theme::Size::statusDot, Theme::Size::statusDot);
    status_row->addWidget(status_dot_);

    status_label_ = new QLabel(this);
    status_label_->setObjectName(QStringLiteral("modelRowStatus"));
    status_row->addWidget(status_label_);

    status_row->addStretch(1);
    actions_col->addLayout(status_row);

    progress_bar_ = new QProgressBar(this);
    progress_bar_->setObjectName(QStringLiteral("rowProgress"));
    progress_bar_->setRange(0, 1);
    progress_bar_->setValue(0);
    progress_bar_->setTextVisible(false);
    progress_bar_->setVisible(false);
    progress_bar_->setAccessibleName(QStringLiteral("Model download progress"));
    actions_col->addWidget(progress_bar_);

    auto *button_row = new QHBoxLayout();
    button_row->setSpacing(Theme::Space::sm);
    button_row->setAlignment(Qt::AlignRight);

    load_btn_ = new QPushButton(QStringLiteral("Load"), this);
    load_btn_->setObjectName(QStringLiteral("primaryButton"));
    load_btn_->setCursor(Qt::PointingHandCursor);
    load_btn_->setToolTip(QStringLiteral("Load this model into the runtime"));
    load_btn_->setAccessibleName(QStringLiteral("Load %1").arg(display_name));
    button_row->addWidget(load_btn_);

    download_btn_ = new QPushButton(QStringLiteral("Download"), this);
    download_btn_->setObjectName(QStringLiteral("primaryButton"));
    download_btn_->setCursor(Qt::PointingHandCursor);
    download_btn_->setToolTip(
        QStringLiteral("Download this model file into the model storage folder"));
    download_btn_->setAccessibleName(QStringLiteral("Download %1").arg(display_name));
    button_row->addWidget(download_btn_);

    unload_btn_ = new QPushButton(QStringLiteral("Unload"), this);
    unload_btn_->setCursor(Qt::PointingHandCursor);
    unload_btn_->setToolTip(QStringLiteral("Unload the model from the runtime"));
    unload_btn_->setAccessibleName(QStringLiteral("Unload %1").arg(display_name));
    button_row->addWidget(unload_btn_);

    delete_btn_ = new QPushButton(QStringLiteral("Delete"), this);
    delete_btn_->setObjectName(QStringLiteral("dangerButton"));
    delete_btn_->setCursor(Qt::PointingHandCursor);
    delete_btn_->setToolTip(
        QStringLiteral("Delete the downloaded model file (requires confirmation)"));
    delete_btn_->setAccessibleName(QStringLiteral("Delete %1").arg(display_name));
    button_row->addWidget(delete_btn_);

    cancel_btn_ = new QPushButton(QStringLiteral("Cancel"), this);
    cancel_btn_->setCursor(Qt::PointingHandCursor);
    cancel_btn_->setToolTip(QStringLiteral("Stop the active download"));
    cancel_btn_->setAccessibleName(QStringLiteral("Cancel download of %1").arg(display_name));
    cancel_btn_->setVisible(false);
    button_row->addWidget(cancel_btn_);

    actions_col->addLayout(button_row);
    row_layout->addLayout(actions_col);

    // ── Signals ─────────────────────────────────────────────────────────
    connect(load_btn_, &QPushButton::clicked, this, [this]() {
        emit loadClicked(model_id_);
    });
    connect(download_btn_, &QPushButton::clicked, this, [this]() {
        emit downloadClicked(model_id_);
    });
    connect(unload_btn_, &QPushButton::clicked, this, [this]() {
        emit unloadClicked(model_id_);
    });
    connect(delete_btn_, &QPushButton::clicked, this, [this]() {
        emit deleteClicked(model_id_);
    });
    connect(cancel_btn_, &QPushButton::clicked, this, [this]() {
        emit cancelDownloadClicked(model_id_);
    });

    updatePresentation();
}

// ── State ───────────────────────────────────────────────────────────────────

void ModelRow::setStatus(Status status, const QString &detail) {
    status_ = status;
    QString text;
    switch (status_) {
        case Status::Loaded:
            text = QStringLiteral("Loaded");
            break;
        case Status::Ready:
            text = QStringLiteral("Ready to load");
            break;
        case Status::NotDownloaded:
            text = QStringLiteral("Not downloaded");
            break;
        case Status::Unavailable:
            text = detail.isEmpty() ? QStringLiteral("Unavailable") : detail;
            break;
    }
    status_label_->setText(text);
    status_label_->setToolTip(text);
    status_dot_->setProperty("dotState",
                             status_ == Status::Loaded        ? QStringLiteral("success")
                             : status_ == Status::Ready       ? QStringLiteral("success")
                             : status_ == Status::Unavailable ? QStringLiteral("warning")
                                                              : QStringLiteral("muted"));
    repolish(status_dot_);
    updatePresentation();
}

void ModelRow::setActivity(Activity activity) {
    activity_ = activity;
    updatePresentation();
}

void ModelRow::setConfigured(bool configured) {
    configured_ = configured;
    setProperty("modelRowState",
                configured_ ? QStringLiteral("configured") : QStringLiteral("normal"));
    repolish(this);
    configured_badge_->setVisible(configured_);
}

void ModelRow::setFilePresent(bool present) {
    file_present_ = present;
    updatePresentation();
}

void ModelRow::setLibraryBusy(bool busy) {
    library_busy_ = busy;
    updatePresentation();
}

void ModelRow::setInferenceBusy(bool busy) {
    inference_busy_ = busy;
    updatePresentation();
}

void ModelRow::setDownloadProgress(qint64 downloaded, qint64 total) {
    download_done_ = downloaded;
    download_total_ = total;
    updatePresentation();
}

// ── Presentation ────────────────────────────────────────────────────────────

void ModelRow::updatePresentation() {
    const bool downloading = activity_ == Activity::Downloading;
    // Only one operation is allowed at a time: while any lifecycle work runs
    // on this row (or anywhere in the library), competing actions are off;
    // live inference additionally gates every lifecycle action.
    const bool idle = activity_ == Activity::Idle;
    const bool actions_enabled = idle && !library_busy_ && !inference_busy_;

    progress_bar_->setVisible(downloading);
    cancel_btn_->setVisible(downloading);
    download_btn_->setVisible(status_ == Status::NotDownloaded);
    load_btn_->setVisible(status_ == Status::Ready || status_ == Status::Loaded);
    unload_btn_->setVisible(status_ == Status::Loaded);

    if (downloading) {
        // Bytes are qint64; project progress as a bounded 0..1000 permille
        // fraction in long double so no int arithmetic can overflow, then
        // clamp into the bar's fixed range.
        if (download_total_ > 0 && download_done_ >= 0) {
            long double permille =
                static_cast<long double>(download_done_) * 1000.0L /
                static_cast<long double>(download_total_);
            if (permille < 0.0L) {
                permille = 0.0L;
            } else if (permille > 1000.0L) {
                permille = 1000.0L;
            }
            const int permille_value = static_cast<int>(permille);
            progress_bar_->setRange(0, 1000);
            progress_bar_->setValue(permille_value);
            status_label_->setText(
                QStringLiteral("Downloading %1%").arg(permille_value / 10));
        } else {
            // Unknown total: indeterminate busy bar, no percentage.
            progress_bar_->setRange(0, 0);
            status_label_->setText(QStringLiteral("Downloading\u2026"));
        }
    } else if (activity_ == Activity::Loading) {
        status_label_->setText(QStringLiteral("Loading\u2026"));
    } else if (activity_ == Activity::Unloading) {
        status_label_->setText(QStringLiteral("Unloading\u2026"));
    }

    // Load: safe only when no operation or inference is active and the file
    // is present.
    load_btn_->setEnabled(actions_enabled && status_ == Status::Ready);
    // Download: safe only when idle and nothing is in flight.
    download_btn_->setEnabled(actions_enabled && status_ == Status::NotDownloaded);
    // Unload: safe only when idle (never while another load/download or a
    // translation run is active).
    unload_btn_->setEnabled(actions_enabled);
    // Delete: a loaded model cannot be deleted, and the file must actually
    // exist; while any lifecycle operation runs the file must not be mutated.
    delete_btn_->setEnabled(idle && !library_busy_ && file_present_ &&
                            status_ != Status::Loaded);
    // Cancel is the escape hatch for the single active download.
    cancel_btn_->setEnabled(downloading);
}
