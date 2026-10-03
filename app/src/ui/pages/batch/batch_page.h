#pragma once

#include <QWidget>

#include <QStringList>
#include <QVariantList>

class BatchQueueModel;
class BatchQueueSortProxy;
class BatchTableView;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;

// Table-based batch work queue.
//
// Rows come from complete queue snapshots (BatchController::queueSnapshot)
// and are keyed by stable durable entry ids; the view never manages card
// widgets, so selection, scroll and focus survive every state push. Sorting
// is display-only through a proxy — the durable queue order never changes.
//
// The fixed language selectors apply only to *new* enqueues (file dialog or
// drag/drop both snapshot the selectors at enqueue time); queued items keep
// the languages they were added with and are never rewritten.
class BatchPage : public QWidget {
    Q_OBJECT

public:
    explicit BatchPage(QWidget *parent = nullptr);

    // Drive the table from a complete durable-queue snapshot (UI thread).
    void setEntries(const QVariantList &entries);
    void setRunning(bool running);
    void setPaused(bool paused);
    void setStatusText(const QString &text);
    // Default languages for new enqueues; follows the Translate page.
    void setDefaultLanguages(const QString &source_lang, const QString &target_lang);

    QString sourceLanguageName() const;
    QString targetLanguageName() const;

signals:
    void addFilesRequested(const QStringList &paths, const QString &source_lang,
                           const QString &target_lang);
    void removeRequested(const QStringList &entry_ids);
    void retryRequested(const QStringList &entry_ids);
    void startRequested();
    void pauseRequested();
    void resumeRequested();

private slots:
    void onAddFiles();
    void onRemove();
    void onRetry();
    void onSelectAll();
    void onClearSelection();
    void onStartPause();

private:
    void updateActions();
    void updateSummary();
    void updateEmptyState();
    void showContextMenu(const QPoint &pos);
    QStringList selectedIds() const;
    void openSelectedSources();
    void openSelectedOutputs();
    bool eventFilter(QObject *watched, QEvent *event) override;

    BatchQueueModel *model_ = nullptr;
    BatchQueueSortProxy *proxy_ = nullptr;
    BatchTableView *view_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QWidget *empty_state_ = nullptr;

    QComboBox *source_combo_ = nullptr;
    QComboBox *target_combo_ = nullptr;
    QPushButton *add_button_ = nullptr;
    QPushButton *remove_button_ = nullptr;
    QPushButton *retry_button_ = nullptr;
    QPushButton *open_source_button_ = nullptr;
    QPushButton *open_output_button_ = nullptr;
    QPushButton *start_pause_button_ = nullptr;
    QLabel *status_label_ = nullptr;
    QLabel *summary_label_ = nullptr;
    QProgressBar *overall_progress_ = nullptr;

    bool running_ = false;
    bool paused_ = false;
};
