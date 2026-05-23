#include "app/task_service.h"

#include <QMetaObject>

TaskService::TaskService(QObject * parent)
    : QObject(parent) {
    wireCallbacks();
}

void TaskService::wireCallbacks() {
    TaskOrchestratorCallbacks callbacks{};

    callbacks.on_status = [this](const std::string & message, bool busy) {
        emit statusChanged(
            QString::fromUtf8(message.c_str(), static_cast<int>(message.size())),
            busy);
    };

    callbacks.on_model_load_finished = [this](bool success, const std::string & error_message) {
        emit modelLoadFinished(
            success,
            QString::fromUtf8(error_message.c_str(), static_cast<int>(error_message.size())));
    };

    callbacks.on_model_unload_finished = [this]() {
        emit modelUnloadFinished();
    };

    callbacks.on_download_progress =
        [this](
            std::int64_t downloaded_bytes,
            std::int64_t total_bytes,
            double speed_bps,
            double eta_seconds) {
            emit downloadProgress(downloaded_bytes, total_bytes, speed_bps, eta_seconds);
        };

    callbacks.on_download_finished = [this](bool success) {
        emit downloadFinished(success);
    };

    callbacks.on_task_state_changed = [this](std::uint64_t task_id, TaskState state) {
        emit taskStateChanged(task_id, static_cast<int>(state));
    };

    callbacks.on_target_reset = [this](std::uint64_t task_id) {
        emit targetReset(task_id);
    };

    callbacks.on_target_appended = [this](std::uint64_t task_id, const std::string & piece) {
        emit targetAppended(
            task_id,
            QString::fromUtf8(piece.c_str(), static_cast<int>(piece.size())));
    };

    callbacks.on_back_translate_reset = [this](std::uint64_t task_id) {
        emit backTranslateReset(task_id);
    };

    callbacks.on_back_translate_appended = [this](std::uint64_t task_id, const std::string & piece) {
        emit backTranslateAppended(
            task_id,
            QString::fromUtf8(piece.c_str(), static_cast<int>(piece.size())));
    };

    callbacks.on_translation_finished = [this](std::uint64_t task_id, TaskState state) {
        emit translationFinished(task_id, static_cast<int>(state));
    };

    orchestrator_.set_callbacks(std::move(callbacks));
}

void TaskService::setModelPath(const QString & path) {
    orchestrator_.set_model_path(path.toUtf8().constData());
}

void TaskService::setRemoteSpec(const QString & spec) {
    orchestrator_.set_remote_spec(spec.toUtf8().constData());
}

void TaskService::setDownloadHub(int hub) {
    orchestrator_.set_download_hub(hub);
}

void TaskService::scheduleProcessNext() {
    QMetaObject::invokeMethod(this, "processNext", Qt::QueuedConnection);
}

TaskId TaskService::submitDownloadModel(TaskPriority priority) {
    const TaskId id = orchestrator_.submit_download_model(priority);
    scheduleProcessNext();
    return id;
}

TaskId TaskService::submitLoadModel(TaskPriority priority) {
    const TaskId id = orchestrator_.submit_load_model(priority);
    scheduleProcessNext();
    return id;
}

TaskId TaskService::submitUnloadModel(TaskPriority priority) {
    const TaskId id = orchestrator_.submit_unload_model(priority);
    scheduleProcessNext();
    return id;
}

TaskId TaskService::submitTranslatePipeline(
    const TranslatePipelinePayload & payload,
    TaskPriority priority) {
    const TaskId id = orchestrator_.submit_translate_pipeline(payload, priority);
    scheduleProcessNext();
    return id;
}

bool TaskService::cancel(TaskId id) {
    return orchestrator_.cancel(id);
}

TaskState TaskService::taskState(TaskId id) const {
    return orchestrator_.task_state(id);
}

bool TaskService::isModelLoaded() const {
    return orchestrator_.is_model_loaded();
}

void TaskService::downloadModel() {
    submitDownloadModel(TaskPriority::Interactive);
}

void TaskService::loadModel() {
    submitLoadModel(TaskPriority::Interactive);
}

void TaskService::unloadModel() {
    submitUnloadModel(TaskPriority::Interactive);
}

void TaskService::cancelTask(quint64 task_id) {
    TaskId id{};
    id.value = task_id;
    if (id.is_valid()) {
        cancel(id);
        return;
    }
    orchestrator_.cancel_running();
}

void TaskService::translateInteractive(
    const QString & source,
    const QString & target_language,
    const QString & source_language,
    bool back_translate) {
    TranslatePipelinePayload payload{};
    payload.source = source.toUtf8().constData();
    payload.target_language = target_language.toUtf8().constData();
    payload.source_language = source_language.toUtf8().constData();
    payload.back_translate = back_translate;
    const TaskId task_id = submitTranslatePipeline(payload, TaskPriority::Interactive);
    emit translateTaskStarted(task_id.value);
}

void TaskService::processNext() {
    orchestrator_.process_next();
}
