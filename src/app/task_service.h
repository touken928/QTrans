#pragma once

#include "task/task_orchestrator.h"
#include "task/task_types.h"

#include <QObject>
#include <QString>

class TaskService : public QObject {
    Q_OBJECT

public:
    explicit TaskService(QObject * parent = nullptr);

    void setModelPath(const QString & path);
    void setRemoteSpec(const QString & spec);
    void setDownloadHub(int hub);

    TaskId submitDownloadModel(TaskPriority priority = TaskPriority::Interactive);
    TaskId submitLoadModel(TaskPriority priority = TaskPriority::Interactive);
    TaskId submitUnloadModel(TaskPriority priority = TaskPriority::Interactive);
    TaskId submitTranslatePipeline(
        const TranslatePipelinePayload & payload,
        TaskPriority priority = TaskPriority::Interactive);

    bool cancel(TaskId id);
    TaskState taskState(TaskId id) const;
    bool isModelLoaded() const;

public slots:
    void processNext();
    void downloadModel();
    void loadModel();
    void unloadModel();
    void cancelTask(quint64 task_id);
    void translateInteractive(
        const QString & source,
        const QString & target_language,
        const QString & source_language,
        bool back_translate);

signals:
    void translateTaskStarted(quint64 task_id);
    void statusChanged(const QString & message, bool busy);
    void modelLoadFinished(bool success, const QString & error_message);
    void modelUnloadFinished();
    void downloadProgress(qint64 downloaded_bytes, qint64 total_bytes, double speed_bps, double eta_seconds);
    void downloadFinished(bool success);
    void taskStateChanged(quint64 task_id, int state);
    void targetReset(quint64 task_id);
    void targetAppended(quint64 task_id, const QString & piece);
    void backTranslateReset(quint64 task_id);
    void backTranslateAppended(quint64 task_id, const QString & piece);
    void translationFinished(quint64 task_id, int state);

private:
    void scheduleProcessNext();
    void wireCallbacks();

    TaskOrchestrator orchestrator_;
};
