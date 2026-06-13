#include "ui/pages/batch/batch_page.h"
#include "ui/pages/batch/batch_card.h"
#include "ui/shared/theme/theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

BatchPage::BatchPage(QWidget *parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("page"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(Theme::Space::xxl, Theme::Space::xxl,
                             Theme::Space::xxl, Theme::Space::xl);
    root->setSpacing(Theme::Space::md);

    // ── Toolbar ─────────────────────────────────────────────────────────
    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(Theme::Space::sm);

    add_button_ = new QPushButton(QStringLiteral("Add File"), this);
    add_button_->setCursor(Qt::PointingHandCursor);
    toolbar->addWidget(add_button_);

    remove_button_ = new QPushButton(QStringLiteral("Delete"), this);
    remove_button_->setCursor(Qt::PointingHandCursor);
    remove_button_->setEnabled(false);
    toolbar->addWidget(remove_button_);

    save_button_ = new QPushButton(QStringLiteral("Save"), this);
    save_button_->setCursor(Qt::PointingHandCursor);
    save_button_->setEnabled(false);
    toolbar->addWidget(save_button_);

    toolbar->addStretch(1);
    root->addLayout(toolbar);

    // ── Card list inside a transparent scroll area ──────────────────────
    scroll_ = new QScrollArea(this);
    scroll_->setObjectName(QStringLiteral("batchScroll"));
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    scroll_->setStyleSheet(QStringLiteral(
        "QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; }"));

    card_container_ = new QWidget(scroll_);
    card_container_->setObjectName(QStringLiteral("batchCardContainer"));
    card_container_->setStyleSheet(QStringLiteral("background: transparent;"));
    card_layout_ = new QVBoxLayout(card_container_);
    card_layout_->setContentsMargins(0, 0, 0, 0);
    card_layout_->setSpacing(Theme::Space::xs);
    card_layout_->addStretch(1);

    scroll_->setWidget(card_container_);
    root->addWidget(scroll_, 1);

    // ── Footer with Start/Pause right-aligned ───────────────────────────
    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("pageFooter"));
    auto *footer_layout = new QHBoxLayout(footer);
    footer_layout->setContentsMargins(0, 0, 0, 0);
    footer_layout->setSpacing(Theme::Space::md);

    status_label_ = new QLabel(QStringLiteral("No files added"), footer);
    status_label_->setObjectName(QStringLiteral("statusLabel"));
    footer_layout->addWidget(status_label_, 1);

    start_pause_button_ = new QPushButton(QStringLiteral("Start"), footer);
    start_pause_button_->setCursor(Qt::PointingHandCursor);
    start_pause_button_->setEnabled(false);
    footer_layout->addWidget(start_pause_button_);

    root->addWidget(footer);

    // ── Connections ─────────────────────────────────────────────────────
    connect(add_button_, &QPushButton::clicked, this, &BatchPage::onAddClicked);
    connect(remove_button_, &QPushButton::clicked, this, &BatchPage::onRemoveClicked);
    connect(save_button_, &QPushButton::clicked, this, &BatchPage::onSaveClicked);
    connect(start_pause_button_, &QPushButton::clicked, this, &BatchPage::onStartPauseClicked);
}

BatchPage::~BatchPage() = default;

// ── State updates ───────────────────────────────────────────────────────────

void BatchPage::setRunning(bool running) {
    running_ = running;
    updateActionButtons();
}
void BatchPage::setPaused(bool paused) {
    paused_ = paused;
    updateActionButtons();
}

void BatchPage::addCard(const QString &entry_id, const QString &file_name,
                        const QString &source_lang, const QString &target_lang) {
    auto *card = new BatchCard(entry_id, file_name, source_lang, target_lang, card_container_);
    connect(card, &BatchCard::clicked, this, &BatchPage::onCardClicked);
    card_layout_->insertWidget(card_layout_->count() - 1, card);
    cards_.insert(entry_id, card);
    status_label_->setText(QStringLiteral("%1 file(s) in queue").arg(cards_.size()));
    updateActionButtons();
}

void BatchPage::removeCard(const QString &entry_id) {
    auto *card = cards_.take(entry_id);
    if (!card) return;
    selected_ids_.remove(entry_id);
    card_layout_->removeWidget(card);
    card->deleteLater();
    status_label_->setText(QStringLiteral("%1 file(s) in queue").arg(cards_.size()));
    updateActionButtons();
}

void BatchPage::setCardState(const QString &entry_id, int state) {
    auto *card = cardForId(entry_id);
    if (card) card->setState(state);
}

void BatchPage::setCardProgress(const QString &entry_id, int completed, int total) {
    auto *card = cardForId(entry_id);
    if (card) card->setProgress(completed, total);
}

void BatchPage::setCardSaved(const QString &entry_id, const QString &output_path) {
    auto *card = cardForId(entry_id);
    if (card) card->setSaved(true, output_path);
}

void BatchPage::setStatusText(const QString &text) {
    status_label_->setText(text);
}

QStringList BatchPage::selectedEntryIds() const {
    QStringList ids;
    ids.reserve(selected_ids_.size());
    for (const auto &id : selected_ids_) ids.append(id);
    return ids;
}

// ── Multi-selection ─────────────────────────────────────────────────────────

void BatchPage::onCardClicked(const QString &entry_id) {
    toggleSelection(entry_id);
}

void BatchPage::toggleSelection(const QString &entry_id) {
    auto *card = cardForId(entry_id);
    if (!card) return;
    if (selected_ids_.contains(entry_id)) {
        selected_ids_.remove(entry_id);
        card->setSelected(false);
    } else {
        selected_ids_.insert(entry_id);
        card->setSelected(true);
    }
    updateActionButtons();
}

void BatchPage::clearSelection() {
    for (const auto &id : selected_ids_) {
        auto *card = cardForId(id);
        if (card) card->setSelected(false);
    }
    selected_ids_.clear();
    updateActionButtons();
}

// ── Slot helpers ───────────────────────────────────────────────────────────

void BatchPage::onAddClicked() {
    emit addFilesRequested();
}

void BatchPage::onRemoveClicked() {
    if (selected_ids_.isEmpty()) return;
    emit removeSelectedRequested(selectedEntryIds());
    clearSelection();
}

void BatchPage::onStartPauseClicked() {
    if (running_) {
        emit paused_ ? resumeRequested() : pauseRequested();
    } else {
        emit startRequested();
    }
}

void BatchPage::onSaveClicked() {
    if (!selected_ids_.isEmpty()) {
        emit saveRequested(selectedEntryIds());
    }
}

// ── Private helpers ────────────────────────────────────────────────────────

BatchCard *BatchPage::cardForId(const QString &entry_id) const {
    return cards_.value(entry_id, nullptr);
}

void BatchPage::updateActionButtons() {
    const int sel_count = selected_ids_.size();
    const bool any_sel = sel_count > 0;

    remove_button_->setEnabled(any_sel);
    save_button_->setEnabled(any_sel);
    add_button_->setEnabled(!running_);

    // Start / Pause in footer
    if (running_) {
        start_pause_button_->setText(paused_ ? QStringLiteral("Resume") : QStringLiteral("Pause"));
    } else {
        start_pause_button_->setText(QStringLiteral("Start"));
    }
    start_pause_button_->setEnabled(cards_.size() > 0);

    status_label_->setText(sel_count > 0
                               ? QStringLiteral("%1 of %2 selected").arg(sel_count).arg(cards_.size())
                               : QStringLiteral("%1 file(s) in queue").arg(cards_.size()));
}
