#include "ui/pages/models/model_page.h"
#include "ui/shared/theme/theme.h"
#include "shared/string_bridge.h"
#include "domain/inference/inference_resolver.h"
#include "domain/model-catalog/model_catalog.h"
#include "domain/inference/runtime_capabilities.h"
#include "domain/download/download.h"

#include <QDesktopServices>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

// =============================================================================
// Internal helpers
// =============================================================================

namespace {

constexpr int kCardMinHeight = 80;

// Build a rich status label with a coloured dot prefix.
QLabel *makeStatusBadge(const QString &text, const QString &dot_color,
                        const QString &text_color, QWidget *parent) {
    auto *label = new QLabel(
        QStringLiteral("<span style='color:%1; font-size:18px;'>&#9679;</span>"
                       " <span style='color:%2;'>%3</span>")
            .arg(dot_color, text_color, text),
        parent);
    label->setTextFormat(Qt::RichText);
    label->setContentsMargins(0, 0, 0, 0);
    return label;
}

void setCardSelected(QWidget *card, bool selected) {
    if (selected) {
        card->setStyleSheet(
            QStringLiteral("QFrame#modelCard { border: 2px solid %1; }")
                .arg(Theme::Color::primary));
    } else {
        card->setStyleSheet(QString{});
    }
}

}  // namespace

// =============================================================================
// ModelPage
// =============================================================================

ModelPage::ModelPage(QWidget *parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("page"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(Theme::Space::xxl, Theme::Space::xxl,
                             Theme::Space::xxl, Theme::Space::xxl);
    root->setSpacing(Theme::Space::lg);

    // ── Directory picker row ──────────────────────────────────────────
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
    dir_row->addWidget(browse_btn_);

    auto *hint = new QLabel(
        QStringLiteral("Model files are downloaded from HuggingFace or ModelSpec to this directory."),
        dir_card);
    hint->setObjectName(QStringLiteral("mutedLabel"));
    hint->setWordWrap(true);

    dir_section->addLayout(dir_row);
    dir_section->addWidget(hint);
    root->addWidget(dir_card);

    // ── Model cards area (scrollable) ─────────────────────────────────
    auto *models_title = new QLabel(QStringLiteral("Available Models"), this);
    models_title->setObjectName(QStringLiteral("sectionTitle"));
    root->addWidget(models_title);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll_->viewport()->setAutoFillBackground(false);

    cards_container_ = new QWidget(scroll_);
    cards_container_->setObjectName(QStringLiteral("modelCardsContainer"));
    cards_layout_ = new QVBoxLayout(cards_container_);
    cards_layout_->setContentsMargins(0, 0, 0, 0);
    cards_layout_->setSpacing(Theme::Space::md);
    cards_layout_->addStretch(1);

    scroll_->setWidget(cards_container_);
    root->addWidget(scroll_, 1);

    // ── Connections ───────────────────────────────────────────────────
    connect(browse_btn_, &QPushButton::clicked, this, &ModelPage::chooseModelsDir);
}

// ── Public API ──────────────────────────────────────────────────────────────

void ModelPage::setSettings(const AppPaths &paths, const AppSettings &settings) {
    paths_ = paths;
    settings_ = settings;

    const QSignalBlocker block(dir_edit_);
    dir_edit_->setText(qtrans::app::from_utf8(settings_.effectiveModelsDir(paths_)));

    rebuildCards();
    refreshCardStates();
}

void ModelPage::setRuntimeCapabilities(const RuntimeCapabilities &caps) {
    has_runtime_caps_ = true;
    runtime_caps_ = &caps;
    refreshCardStates();
}

void ModelPage::setLoadedModelId(const QString &model_id) {
    loaded_model_id_ = model_id;
    refreshCardStates();
}

void ModelPage::setModelLoaded(bool loaded) {
    model_loaded_ = loaded;
    if (!loaded) {
        loaded_model_id_.clear();
    }
    refreshCardStates();
}

void ModelPage::setBusy(bool busy) {
    busy_ = busy;
    refreshCardStates();
}

void ModelPage::applyTo(AppSettings &settings) const {
    const QString dir = dir_edit_->text().trimmed();
    settings.setEffectiveModelsDir(paths_, qtrans::app::to_utf8(dir));
    settings.model_id = settings_.model_id;
}

// ── Internal helpers ────────────────────────────────────────────────────────

void ModelPage::rebuildCards() {
    // Destroy existing cards
    for (auto it = cards_.begin(); it != cards_.end(); ++it) {
        CardWidgets &c = it.value();
        if (c.frame != nullptr) {
            cards_layout_->removeWidget(c.frame);
            c.frame->deleteLater();
        }
    }
    cards_.clear();

    // Remove the stretch item before adding cards, re-add after
    QLayoutItem *spacer_item = nullptr;
    if (cards_layout_->count() > 0) {
        QLayoutItem *item = cards_layout_->itemAt(cards_layout_->count() - 1);
        if (item->spacerItem() != nullptr) {
            spacer_item = cards_layout_->takeAt(cards_layout_->count() - 1);
        }
    }

    for (const ModelCatalogEntry &entry : model_catalog()) {
        const QString model_id = qtrans::app::from_utf8(entry.id);
        const QString display_name = qtrans::app::from_utf8(entry.display_name);
        const QString filename = qtrans::app::from_utf8(entry.filename);

        // ── Card frame ────────────────────────────────────────────
        auto *card = new QFrame(cards_container_);
        card->setObjectName(QStringLiteral("modelCard"));
        card->setMinimumHeight(kCardMinHeight);

        auto *card_row = new QHBoxLayout(card);
        card_row->setContentsMargins(Theme::Space::lg, Theme::Space::md,
                                     Theme::Space::lg, Theme::Space::md);
        card_row->setSpacing(Theme::Space::lg);

        // Left: model info
        auto *info_col = new QVBoxLayout();
        info_col->setSpacing(Theme::Space::xs);

        auto *name_label = new QLabel(display_name, card);
        name_label->setObjectName(QStringLiteral("modelCardName"));
        info_col->addWidget(name_label);

        auto *file_label = new QLabel(filename, card);
        file_label->setObjectName(QStringLiteral("modelCardFile"));
        info_col->addWidget(file_label);

        // Backend availability hint
        const bool available = modelIsAvailable(model_id);
        if (!available && has_runtime_caps_ && runtime_caps_ != nullptr) {
            auto *hint_label = new QLabel(
                unavailable_reason(entry, *runtime_caps_).c_str(), card);
            hint_label->setObjectName(QStringLiteral("mutedLabel"));
            hint_label->setWordWrap(true);
            info_col->addWidget(hint_label);
        }

        card_row->addLayout(info_col, 1);

        // Right: status + actions
        auto *actions_col = new QVBoxLayout();
        actions_col->setSpacing(Theme::Space::sm);
        actions_col->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        // Status badge
        auto *badge = new QLabel(card);
        badge->setTextFormat(Qt::RichText);
        badge->setContentsMargins(0, 0, 0, 0);
        actions_col->addWidget(badge);

        // Buttons row
        auto *btn_row = new QHBoxLayout();
        btn_row->setSpacing(Theme::Space::sm);
        btn_row->setAlignment(Qt::AlignRight);

        auto *load_btn = new QPushButton(QStringLiteral("Load"), card);
        load_btn->setObjectName(QStringLiteral("primaryButton"));
        load_btn->setCursor(Qt::PointingHandCursor);
        btn_row->addWidget(load_btn);

        auto *unload_btn = new QPushButton(QStringLiteral("Unload"), card);
        unload_btn->setCursor(Qt::PointingHandCursor);
        btn_row->addWidget(unload_btn);

        auto *delete_btn = new QPushButton(QStringLiteral("Delete"), card);
        delete_btn->setObjectName(QStringLiteral("dangerButton"));
        delete_btn->setCursor(Qt::PointingHandCursor);
        btn_row->addWidget(delete_btn);

        actions_col->addLayout(btn_row);
        card_row->addLayout(actions_col);

        cards_layout_->addWidget(card);

        // Store widgets
        CardWidgets cw;
        cw.frame = card;
        cw.status_badge = badge;
        cw.load_btn = load_btn;
        cw.unload_btn = unload_btn;
        cw.delete_btn = delete_btn;
        cards_.insert(model_id, cw);

        // Connections per card
        const QString captured_id = model_id;
        connect(load_btn, &QPushButton::clicked, this, [this, captured_id]() {
            settings_.setSelectedModelId(qtrans::app::to_utf8(captured_id));
            emit loadModelRequested(captured_id);
        });
        connect(unload_btn, &QPushButton::clicked, this, [this, captured_id]() {
            emit unloadModelRequested(captured_id);
        });
        connect(delete_btn, &QPushButton::clicked, this, [this, captured_id]() {
            emit deleteModelRequested(captured_id);
        });
    }

    // Re-add spacer
    if (spacer_item != nullptr) {
        cards_layout_->addItem(spacer_item);
    }
}

void ModelPage::refreshCardStates() {
    const bool any_loaded = model_loaded_;
    const bool idle = !busy_;

    for (const ModelCatalogEntry &entry : model_catalog()) {
        const QString model_id = qtrans::app::from_utf8(entry.id);
        auto it = cards_.find(model_id);
        if (it == cards_.end()) {
            continue;
        }

        CardWidgets &c = it.value();
        const bool is_loaded = any_loaded && loaded_model_id_ == model_id;
        const bool is_this_loaded = is_loaded;
        const bool exists = modelFileExists(model_id);
        const bool available = modelIsAvailable(model_id);
        const bool is_selected = settings_.model_id == qtrans::app::to_utf8(model_id);

        // Card selected visual
        setCardSelected(c.frame, is_selected);

        // Status badge
        if (is_this_loaded) {
            c.status_badge->setText(
                QStringLiteral("<span style='color:#30d158; font-size:18px;'>&#9679;</span>"
                               " <span style='color:#1d1d1f; font-weight:bold;'>Loaded</span>"));
        } else if (exists) {
            c.status_badge->setText(
                QStringLiteral("<span style='color:#0071e3; font-size:18px;'>&#9679;</span>"
                               " <span style='color:#0071e3;'>Ready</span>"));
        } else {
            c.status_badge->setText(
                QStringLiteral("<span style='color:#aeaeb2; font-size:18px;'>&#9679;</span>"
                               " <span style='color:#aeaeb2;'>Not Downloaded</span>"));
        }

        // Button states
        c.load_btn->setEnabled(idle && available && !is_this_loaded);
        c.unload_btn->setEnabled(idle && is_this_loaded);
        c.delete_btn->setEnabled(idle && exists && !is_this_loaded && available);
    }
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
    refreshCardStates();
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
