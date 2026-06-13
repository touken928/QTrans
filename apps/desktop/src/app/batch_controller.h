#pragma once

#include "app/task_service.h"
#include "domain/batch/batch_store.h"
#include "domain/batch/batch_types.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

#include <filesystem>
#include <string>
#include <vector>

// App-layer batch controller. Owns a BatchStore, submits segments through
// TaskService, and tracks progress. Lives on the worker thread alongside
// TaskService; public API is Q_INVOKABLE for cross-thread use.
class BatchController : public QObject {
    Q_OBJECT

public:
    explicit BatchController(TaskService *taskService,
                             std::filesystem::path queueFilePath,
                             std::filesystem::path outputDir,
                             QObject *parent = nullptr);

    // ── File management ──────────────────────────────────────────────────
    Q_INVOKABLE void addFile(const QString &path,
                             const QString &source_lang,
                             const QString &target_lang);
    Q_INVOKABLE void removeEntry(const QString &entry_id);

    // ── Lifecycle control ────────────────────────────────────────────────
    Q_INVOKABLE void start();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE bool isRunning() const { return running_; }
    Q_INVOKABLE bool isPaused() const  { return paused_; }

    // ── Save / export ────────────────────────────────────────────────────
    // Write the output file for one completed entry to the default output dir.
    Q_INVOKABLE void saveEntry(const QString &entry_id);
    // Write output files for the given list of entry ids to a custom directory.
    Q_INVOKABLE void saveEntriesToDirectory(const QStringList &entry_ids,
                                            const QString &dest_dir);

    // ── Initialisation ───────────────────────────────────────────────────
    // Emit signals for all entries already in the persisted queue so the UI
    // auto-syncs without a manual refresh.  Call once after connecting
    // signals.
    Q_INVOKABLE void loadPersistedEntries();

    // ── Query ────────────────────────────────────────────────────────────
    Q_INVOKABLE QStringList entryIds() const;
    Q_INVOKABLE QString entryFileName(const QString &entry_id) const;
    Q_INVOKABLE QString entryFilePath(const QString &entry_id) const;
    Q_INVOKABLE QString entrySourceLanguage(const QString &entry_id) const;
    Q_INVOKABLE QString entryTargetLanguage(const QString &entry_id) const;
    Q_INVOKABLE int entryState(const QString &entry_id) const;
    Q_INVOKABLE int entryProgress(const QString &entry_id) const;
    Q_INVOKABLE int entryTotal(const QString &entry_id) const;

    // Single-call metadata query for card UIs. Returns keys:
    //   id, file, file_path, source, target, state,
    //   segments_done, segments_total, saved (bool), save_path
    Q_INVOKABLE QVariantMap entryMetadata(const QString &entry_id) const;

signals:
    void entryAdded(const QString &entry_id,
                    const QString &source_language,
                    const QString &target_language);
    void entryRemoved(const QString &entry_id);
    void entryStateChanged(const QString &entry_id, int state);
    void segmentProgress(const QString &entry_id, int completed, int total);
    void entrySaved(const QString &entry_id, const QString &output_path);
    void batchStateChanged(bool running, bool paused);
    void batchFinished();
    void errorOccurred(const QString &message);

private slots:
    void onTranslationFinished(quint64 task_id, int state);
    void onTaskFailed(quint64 task_id, const QString &message);
    void onTargetReset(quint64 task_id);
    void onTargetAppended(quint64 task_id, const QString &piece);
    void onRequeueTimer();

private:
    void advanceBatch();
    void submitNextSegment(const BatchEntry &entry, int segment_index);
    void setEntryState(const std::string &entry_id, BatchEntryState state);
    void writeOutputFile(const BatchEntry &entry);
    void emitBatchState();

    TaskService *taskService_;
    BatchStore store_;
    std::filesystem::path outputDir_;
    QTimer requeueTimer_;

    bool running_ = false;
    bool paused_ = false;

    std::string currentEntryId_;
    int currentSegmentIndex_ = -1;
    TaskId currentTaskId_{};
    QString currentOutputText_;
    QString currentErrorMessage_;
};
