#include "ui/pages/batch/batch_page.h"
#include "ui/pages/batch/batch_queue_model.h"
#include "ui/pages/batch/batch_table_view.h"
#include "ui/shared/theme/theme.h"
#include "ui/shared/widget_utils.h"
#include "domain/batch/batch_enums.h"
#include "domain/model-catalog/language_list.h"
#include "shared/string_bridge.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QPushButton>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {

int defaultLanguageIndex(const char *id) {
    for (int i = 0; i < translation_language_count(); ++i) {
        if (qtrans::app::from_utf8(translation_languages()[i].id) ==
            qtrans::app::from_utf8(id)) {
            return i;
        }
    }
    return 0;
}

}  // namespace

BatchPage::BatchPage(QWidget *parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("page"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(Theme::Space::xxl, Theme::Space::xxl,
                             Theme::Space::xxl, Theme::Space::xl);
    root->setSpacing(Theme::Space::md);

    // ── Language bar: fixed selectors for NEW enqueues only ─────────────
    auto *lang_bar = new QWidget(this);
    lang_bar->setObjectName(QStringLiteral("batchLangBar"));
    auto *lang_layout = new QHBoxLayout(lang_bar);
    lang_layout->setContentsMargins(Theme::Space::lg, Theme::Space::sm,
                                    Theme::Space::lg, Theme::Space::sm);
    lang_layout->setSpacing(Theme::Space::sm);

    auto *lang_caption = new QLabel(QStringLiteral("New files"), lang_bar);
    lang_caption->setObjectName(QStringLiteral("toolbarCaption"));
    lang_layout->addWidget(lang_caption);

    auto *from_caption = new QLabel(QStringLiteral("From"), lang_bar);
    from_caption->setObjectName(QStringLiteral("toolbarCaption"));
    lang_layout->addWidget(from_caption);

    source_combo_ = new QComboBox(lang_bar);
    for (int i = 0; i < translation_language_count(); ++i) {
        source_combo_->addItem(
            qtrans::app::from_utf8(translation_languages()[i].label));
    }
    source_combo_->setCurrentIndex(defaultLanguageIndex("en"));
    source_combo_->setToolTip(
        QStringLiteral("Source language applied to newly added files"));
    source_combo_->setAccessibleName(QStringLiteral("Source language for new files"));
    configureComboBox(source_combo_, 120);
    lang_layout->addWidget(source_combo_);

    auto *to_caption = new QLabel(QStringLiteral("\u2192 To"), lang_bar);
    to_caption->setObjectName(QStringLiteral("toolbarCaption"));
    lang_layout->addWidget(to_caption);

    target_combo_ = new QComboBox(lang_bar);
    for (int i = 0; i < translation_language_count(); ++i) {
        target_combo_->addItem(
            qtrans::app::from_utf8(translation_languages()[i].label));
    }
    target_combo_->setCurrentIndex(defaultLanguageIndex("zh"));
    target_combo_->setToolTip(
        QStringLiteral("Target language applied to newly added files"));
    target_combo_->setAccessibleName(QStringLiteral("Target language for new files"));
    configureComboBox(target_combo_, 120);
    lang_layout->addWidget(target_combo_);

    auto *lang_hint = new QLabel(
        QStringLiteral("Queued files keep the languages they were added with."),
        lang_bar);
    lang_hint->setObjectName(QStringLiteral("mutedLabel"));
    lang_hint->setWordWrap(false);
    lang_layout->addWidget(lang_hint, 1);

    root->addWidget(lang_bar);

    // ── Action toolbar ──────────────────────────────────────────────────
    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(Theme::Space::sm);

    add_button_ = new QPushButton(QStringLiteral("Add Files\u2026"), this);
    add_button_->setObjectName(QStringLiteral("primaryButton"));
    add_button_->setCursor(Qt::PointingHandCursor);
    add_button_->setToolTip(
        QStringLiteral("Choose files to enqueue with the languages above"));
    add_button_->setAccessibleName(QStringLiteral("Add files"));
    toolbar->addWidget(add_button_);

    remove_button_ = new QPushButton(QStringLiteral("Remove"), this);
    remove_button_->setCursor(Qt::PointingHandCursor);
    remove_button_->setToolTip(
        QStringLiteral("Remove the selected entries from the queue"));
    remove_button_->setAccessibleName(QStringLiteral("Remove selected entries"));
    remove_button_->setEnabled(false);
    toolbar->addWidget(remove_button_);

    retry_button_ = new QPushButton(QStringLiteral("Retry"), this);
    retry_button_->setCursor(Qt::PointingHandCursor);
    retry_button_->setToolTip(
        QStringLiteral("Reset the selected failed entries so they can run again"));
    retry_button_->setAccessibleName(QStringLiteral("Retry selected entries"));
    retry_button_->setEnabled(false);
    toolbar->addWidget(retry_button_);

    open_source_button_ = new QPushButton(QStringLiteral("Open Source"), this);
    open_source_button_->setCursor(Qt::PointingHandCursor);
    open_source_button_->setToolTip(
        QStringLiteral("Open the original file of the selected entries"));
    open_source_button_->setAccessibleName(QStringLiteral("Open source files"));
    open_source_button_->setEnabled(false);
    toolbar->addWidget(open_source_button_);

    open_output_button_ = new QPushButton(QStringLiteral("Open Output"), this);
    open_output_button_->setCursor(Qt::PointingHandCursor);
    open_output_button_->setToolTip(
        QStringLiteral("Reveal the translated output of the selected entries"));
    open_output_button_->setAccessibleName(QStringLiteral("Open output folders"));
    open_output_button_->setEnabled(false);
    toolbar->addWidget(open_output_button_);

    toolbar->addStretch(1);

    auto *select_all_button = new QPushButton(QStringLiteral("Select All"), this);
    select_all_button->setCursor(Qt::PointingHandCursor);
    select_all_button->setToolTip(QStringLiteral("Select every entry in the queue"));
    select_all_button->setAccessibleName(QStringLiteral("Select all entries"));
    toolbar->addWidget(select_all_button);

    auto *clear_button = new QPushButton(QStringLiteral("Clear"), this);
    clear_button->setCursor(Qt::PointingHandCursor);
    clear_button->setToolTip(QStringLiteral("Clear the current selection"));
    clear_button->setAccessibleName(QStringLiteral("Clear selection"));
    toolbar->addWidget(clear_button);

    root->addLayout(toolbar);

    // ── Table / empty state ─────────────────────────────────────────────
    model_ = new BatchQueueModel(this);
    proxy_ = new BatchQueueSortProxy(this);
    proxy_->setSourceModel(model_);
    proxy_->setDynamicSortFilter(false);

    view_ = new BatchTableView(this);
    view_->setModel(proxy_);
    view_->setObjectName(QStringLiteral("batchQueueTable"));
    view_->setAlternatingRowColors(true);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setSortingEnabled(true);
    view_->setShowGrid(false);
    view_->setWordWrap(false);
    view_->setCornerButtonEnabled(true);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    view_->setAccessibleName(QStringLiteral("Batch work queue"));
    view_->setAccessibleDescription(
        QStringLiteral("Queued files for batch translation with their language "
                       "pair, state and progress"));
    view_->verticalHeader()->setVisible(false);
    view_->verticalHeader()->setDefaultSectionSize(30);
    view_->horizontalHeader()->setHighlightSections(false);
    view_->horizontalHeader()->setSectionResizeMode(
        BatchQueueModel::ColumnFile, QHeaderView::Stretch);
    view_->horizontalHeader()->setSectionResizeMode(
        BatchQueueModel::ColumnSource, QHeaderView::ResizeToContents);
    view_->horizontalHeader()->setSectionResizeMode(
        BatchQueueModel::ColumnTarget, QHeaderView::ResizeToContents);
    view_->horizontalHeader()->setSectionResizeMode(
        BatchQueueModel::ColumnState, QHeaderView::ResizeToContents);
    view_->horizontalHeader()->setSectionResizeMode(
        BatchQueueModel::ColumnProgress, QHeaderView::ResizeToContents);

    // ── Explicit empty state ────────────────────────────────────────────
    empty_state_ = new QWidget(this);
    auto *empty_layout = new QVBoxLayout(empty_state_);
    empty_layout->setContentsMargins(Theme::Space::xl, Theme::Space::xl,
                                     Theme::Space::xl, Theme::Space::xl);
    empty_layout->setSpacing(Theme::Space::sm);
    empty_layout->addStretch(1);

    auto *empty_title = new QLabel(QStringLiteral("No files in the queue"),
                                   empty_state_);
    empty_title->setObjectName(QStringLiteral("batchEmptyTitle"));
    empty_title->setAlignment(Qt::AlignCenter);
    empty_layout->addWidget(empty_title);

    auto *empty_hint = new QLabel(
        QStringLiteral("Add files with the button above, or drag and drop "
                       "text files here.\nNew files use the languages selected "
                       "at the top."),
        empty_state_);
    empty_hint->setObjectName(QStringLiteral("batchEmptyHint"));
    empty_hint->setAlignment(Qt::AlignCenter);
    empty_hint->setWordWrap(true);
    empty_layout->addWidget(empty_hint);

    empty_layout->addStretch(1);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(view_);
    stack_->addWidget(empty_state_);
    root->addWidget(stack_, 1);

    // ── Footer: summary + overall progress + run control ────────────────
    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("pageFooter"));
    auto *footer_layout = new QHBoxLayout(footer);
    footer_layout->setContentsMargins(0, 0, 0, 0);
    footer_layout->setSpacing(Theme::Space::md);

    status_label_ = new QLabel(QStringLiteral("Queue ready"), footer);
    status_label_->setObjectName(QStringLiteral("statusLabel"));
    status_label_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    footer_layout->addWidget(status_label_, 1);

    summary_label_ = new QLabel(QStringLiteral("0 files"), footer);
    summary_label_->setObjectName(QStringLiteral("batchSummary"));
    summary_label_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    footer_layout->addWidget(summary_label_);

    overall_progress_ = new QProgressBar(footer);
    overall_progress_->setObjectName(QStringLiteral("batchOverallProgress"));
    overall_progress_->setRange(0, 1);
    overall_progress_->setValue(0);
    overall_progress_->setTextVisible(false);
    overall_progress_->setFixedWidth(120);
    overall_progress_->setAccessibleName(QStringLiteral("Overall batch progress"));
    footer_layout->addWidget(overall_progress_);

    start_pause_button_ = new QPushButton(QStringLiteral("Start"), footer);
    start_pause_button_->setObjectName(QStringLiteral("primaryButton"));
    start_pause_button_->setCursor(Qt::PointingHandCursor);
    start_pause_button_->setEnabled(false);
    start_pause_button_->setMinimumWidth(88);
    footer_layout->addWidget(start_pause_button_);

    root->addWidget(footer);

    // ── Connections ─────────────────────────────────────────────────────
    connect(add_button_, &QPushButton::clicked, this, &BatchPage::onAddFiles);
    connect(remove_button_, &QPushButton::clicked, this, &BatchPage::onRemove);
    connect(retry_button_, &QPushButton::clicked, this, &BatchPage::onRetry);
    connect(open_source_button_, &QPushButton::clicked, this, &BatchPage::openSelectedSources);
    connect(open_output_button_, &QPushButton::clicked, this, &BatchPage::openSelectedOutputs);
    connect(select_all_button, &QPushButton::clicked, this, &BatchPage::onSelectAll);
    connect(clear_button, &QPushButton::clicked, this, &BatchPage::onClearSelection);
    connect(start_pause_button_, &QPushButton::clicked, this, &BatchPage::onStartPause);
    connect(view_, &BatchTableView::filesDropped, this, [this](const QStringList &paths) {
        if (!paths.isEmpty()) {
            emit addFilesRequested(paths, sourceLanguageName(), targetLanguageName());
        }
    });
    connect(view_, &BatchTableView::removeSelectionRequested, this, &BatchPage::onRemove);
    connect(view_, &QWidget::customContextMenuRequested, this, &BatchPage::showContextMenu);
    connect(view_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this]() { updateActions(); });
    connect(model_, &QAbstractItemModel::rowsInserted, this, [this]() {
        updateSummary();
        updateEmptyState();
        updateActions();
    });
    connect(model_, &QAbstractItemModel::rowsRemoved, this, [this]() {
        updateSummary();
        updateEmptyState();
        updateActions();
    });
    connect(model_, &QAbstractItemModel::dataChanged, this, [this]() {
        updateSummary();
        updateActions();
    });

    // The empty state is a drop target too: dropping files onto it must use
    // exactly the same enqueue path (and language snapshot) as the table.
    empty_state_->setAcceptDrops(true);
    empty_state_->installEventFilter(this);
}

// ── Public API ──────────────────────────────────────────────────────────────

void BatchPage::setEntries(const QVariantList &entries) {
    model_->applySnapshot(entries);
    updateSummary();
    updateEmptyState();
    updateActions();
}

void BatchPage::setRunning(bool running) {
    running_ = running;
    updateActions();
    if (running) {
        status_label_->setText(paused_ ? QStringLiteral("Batch paused")
                                       : QStringLiteral("Batch running\u2026"));
    }
}

void BatchPage::setPaused(bool paused) {
    paused_ = paused;
    updateActions();
    if (running_ && paused) {
        status_label_->setText(QStringLiteral("Batch paused"));
    }
}

void BatchPage::setStatusText(const QString &text) {
    status_label_->setText(text);
}

void BatchPage::setDefaultLanguages(const QString &source_lang,
                                    const QString &target_lang) {
    const int source_index = source_combo_->findText(source_lang);
    if (source_index >= 0) {
        source_combo_->setCurrentIndex(source_index);
    }
    const int target_index = target_combo_->findText(target_lang);
    if (target_index >= 0) {
        target_combo_->setCurrentIndex(target_index);
    }
}

QString BatchPage::sourceLanguageName() const {
    return source_combo_->currentText();
}

QString BatchPage::targetLanguageName() const {
    return target_combo_->currentText();
}

// ── Slots ───────────────────────────────────────────────────────────────────

void BatchPage::onAddFiles() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Select file(s) for batch translation"), QString(),
        QStringLiteral("Text files (*.txt *.md *.srt);;All files (*)"));
    if (paths.isEmpty()) {
        return;
    }
    emit addFilesRequested(paths, sourceLanguageName(), targetLanguageName());
}

void BatchPage::onRemove() {
    const QStringList ids = selectedIds();
    if (ids.isEmpty()) {
        return;
    }
    emit removeRequested(ids);
}

void BatchPage::onRetry() {
    const QStringList ids = selectedIds();
    if (ids.isEmpty()) {
        return;
    }
    emit retryRequested(ids);
}

void BatchPage::onSelectAll() {
    view_->selectAll();
}

void BatchPage::onClearSelection() {
    view_->clearSelection();
}

void BatchPage::onStartPause() {
    if (running_) {
        if (paused_) {
            emit resumeRequested();
        } else {
            emit pauseRequested();
        }
    } else {
        emit startRequested();
    }
}

// ── Context menu ────────────────────────────────────────────────────────────

void BatchPage::showContextMenu(const QPoint &pos) {
    const QStringList ids = selectedIds();
    if (ids.isEmpty()) {
        return;
    }

    QMenu menu(this);
    menu.setObjectName(QStringLiteral("batchContextMenu"));

    bool any_failed = false;
    bool any_saved = false;
    for (const QString &id : ids) {
        const QVariantMap data = model_->entryData(id);
        const int state = data.value(QStringLiteral("state")).toInt();
        if (state == static_cast<int>(BatchEntryState::Failed)) {
            any_failed = true;
        }
        if (data.value(QStringLiteral("saved")).toBool()) {
            any_saved = true;
        }
    }

    auto *open_source = menu.addAction(QStringLiteral("Open Source File"));
    open_source->setEnabled(ids.size() == 1);
    auto *open_output = menu.addAction(QStringLiteral("Open Output Folder"));
    open_output->setEnabled(any_saved);
    menu.addSeparator();
    auto *retry = menu.addAction(QStringLiteral("Retry"));
    retry->setEnabled(any_failed);
    auto *remove = menu.addAction(QStringLiteral("Remove from Queue"));

    QAction *chosen = menu.exec(view_->viewport()->mapToGlobal(pos));
    if (chosen == nullptr) {
        return;
    }
    if (chosen == open_source && ids.size() == 1) {
        openSelectedSources();
    } else if (chosen == open_output) {
        openSelectedOutputs();
    } else if (chosen == retry) {
        onRetry();
    } else if (chosen == remove) {
        onRemove();
    }
}

// ── Private helpers ─────────────────────────────────────────────────────────

QStringList BatchPage::selectedIds() const {
    QStringList ids;
    const QModelIndexList selected = view_->selectionModel()->selectedRows();
    ids.reserve(selected.size());
    for (const QModelIndex &index : selected) {
        const QString id =
            proxy_->data(index, BatchQueueModel::RoleEntryId).toString();
        if (!id.isEmpty()) {
            ids.append(id);
        }
    }
    return ids;
}

void BatchPage::openSelectedSources() {
    // Same eligibility as the context menu: a single selected entry with a
    // real file path.
    const QStringList ids = selectedIds();
    if (ids.size() != 1) {
        return;
    }
    const QString path =
        model_->entryData(ids.first()).value(QStringLiteral("file_path")).toString();
    if (!path.isEmpty()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void BatchPage::openSelectedOutputs() {
    // Same eligibility as the context menu: any selected entry whose saved
    // output exists; the containing folder is revealed.
    for (const QString &id : selectedIds()) {
        const QVariantMap data = model_->entryData(id);
        const QString path = data.value(QStringLiteral("save_path")).toString();
        if (!path.isEmpty()) {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
        }
    }
}

bool BatchPage::eventFilter(QObject *watched, QEvent *event) {
    if (watched == empty_state_) {
        switch (event->type()) {
            case QEvent::DragEnter:
            case QEvent::DragMove: {
                auto *drop = static_cast<QDropEvent *>(event);
                if (BatchTableView::hasLocalFileDrag(drop->mimeData())) {
                    drop->acceptProposedAction();
                    return true;
                }
                return false;
            }
            case QEvent::Drop: {
                auto *drop = static_cast<QDropEvent *>(event);
                const QStringList paths = BatchTableView::localFilePaths(drop->mimeData());
                if (!paths.isEmpty()) {
                    // Exactly the same enqueue path as table drops: same
                    // signal, same current-language snapshot.
                    emit addFilesRequested(paths, sourceLanguageName(),
                                           targetLanguageName());
                    drop->acceptProposedAction();
                    return true;
                }
                return false;
            }
            default:
                break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void BatchPage::updateActions() {
    const QStringList ids = selectedIds();
    const int selected_count = ids.size();
    const bool any_selected = selected_count > 0;

    bool any_failed = false;
    bool any_saved = false;
    bool any_processing = false;
    for (const QString &id : ids) {
        const QVariantMap data = model_->entryData(id);
        const int state = data.value(QStringLiteral("state")).toInt();
        if (state == static_cast<int>(BatchEntryState::Failed)) {
            any_failed = true;
        }
        if (state == static_cast<int>(BatchEntryState::Processing)) {
            any_processing = true;
        }
        if (data.value(QStringLiteral("saved")).toBool()) {
            any_saved = true;
        }
    }

    // Removing a translating entry cancels its job in the controller; that
    // is safe, but competing actions (retry) must not run against it.
    remove_button_->setEnabled(any_selected);
    retry_button_->setEnabled(any_selected && any_failed && !any_processing);
    open_source_button_->setEnabled(selected_count == 1);
    open_output_button_->setEnabled(any_selected && any_saved);

    add_button_->setEnabled(!running_);

    if (running_) {
        start_pause_button_->setText(paused_ ? QStringLiteral("Resume")
                                             : QStringLiteral("Pause"));
    } else {
        start_pause_button_->setText(QStringLiteral("Start"));
    }
    start_pause_button_->setEnabled(model_->rowCount() > 0);
}

void BatchPage::updateSummary() {
    const QStringList ids = model_->entryIds();
    int completed = 0;
    int failed = 0;
    int processing = 0;
    int segments_done = 0;
    int segments_total = 0;
    for (const QString &id : ids) {
        const QVariantMap data = model_->entryData(id);
        const int state = data.value(QStringLiteral("state")).toInt();
        switch (static_cast<BatchEntryState>(state)) {
            case BatchEntryState::Completed:
                ++completed;
                break;
            case BatchEntryState::Failed:
                ++failed;
                break;
            case BatchEntryState::Processing:
                ++processing;
                break;
            default:
                break;
        }
        segments_done += data.value(QStringLiteral("segments_done")).toInt();
        segments_total += data.value(QStringLiteral("segments_total")).toInt();
    }

    if (ids.isEmpty()) {
        summary_label_->setText(QStringLiteral("0 files"));
        overall_progress_->setRange(0, 1);
        overall_progress_->setValue(0);
        overall_progress_->setVisible(false);
        return;
    }

    QString summary = QStringLiteral("%1 file(s)").arg(ids.size());
    if (processing > 0) {
        summary += QStringLiteral(" \u00B7 %1 translating").arg(processing);
    }
    if (completed > 0) {
        summary += QStringLiteral(" \u00B7 %1 done").arg(completed);
    }
    if (failed > 0) {
        summary += QStringLiteral(" \u00B7 %1 failed").arg(failed);
    }
    summary_label_->setText(summary);

    overall_progress_->setVisible(segments_total > 0);
    overall_progress_->setRange(0, std::max(segments_total, 1));
    overall_progress_->setValue(std::min(segments_done, std::max(segments_total, 1)));
}

void BatchPage::updateEmptyState() {
    stack_->setCurrentWidget(model_->rowCount() == 0 ? empty_state_ : view_);
}
