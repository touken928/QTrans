#include "app/task_service.h"

#include "domain/download/model_downloader.h"
#include "domain/inference/production_translation_session.h"
#include "shared/string_bridge.h"
#include "domain/logging/component.h"
#include "domain/logging/logger.h"
#include <QMetaObject>
#include <QThread>

TaskService::TaskService(QObject *parent)
    : QObject(parent),
      orchestrator_(TaskExecutors{
          std::make_unique<ProductionModelDownloader>(),
          std::make_unique<ProductionTranslationSession>()}) {
    wireCallbacks();
}

void TaskService::wireCallbacks() {
    TaskOrchestratorCallbacks callbacks{};

    callbacks.on_status = [this](const std::string &message, bool busy) {
        emit statusChanged(qtrans::app::from_utf8(message), busy);
    };

    callbacks.on_model_load_finished = [this](bool success, const std::string &error_message) {
        const QString backend_label =
            success ? qtrans::app::from_utf8(orchestrator_.active_backend_label()) : QString{};
        emit modelLoadFinished(success, qtrans::app::from_utf8(error_message), backend_label);
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

    callbacks.on_translation_finished = [this](std::uint64_t task_id, TaskState state) {
        qtrans::log::get(qtrans::log::Component::Task)
            ->debug("translationFinished task:{} state:{}", task_id, static_cast<int>(state));
        emit translationFinished(task_id, static_cast<int>(state));
    };

    callbacks.on_task_failed = [this](std::uint64_t task_id, const std::string &message) {
        emit taskFailed(task_id, qtrans::app::from_utf8(message));
    };

    callbacks.on_target_reset = [this](std::uint64_t task_id) {
        qtrans::log::get(qtrans::log::Component::Task)->debug("targetReset task:{}", task_id);
        emit targetReset(task_id);
    };

    callbacks.on_target_appended = [this](std::uint64_t task_id, const std::string &piece) {
        qtrans::log::get(qtrans::log::Component::Task)
            ->trace("targetAppended task:{} piece:'{}'", task_id, piece);
        emit targetAppended(task_id, qtrans::app::from_utf8(piece));
    };

    callbacks.on_back_translate_reset = [this](std::uint64_t task_id) {
        emit backTranslateReset(task_id);
    };

    callbacks.on_back_translate_appended = [this](std::uint64_t task_id, const std::string &piece) {
        emit backTranslateAppended(task_id, qtrans::app::from_utf8(piece));
    };

    orchestrator_.set_callbacks(std::move(callbacks));
}

void TaskService::setModelPath(const QString &path) {
    orchestrator_.set_model_path(qtrans::app::to_utf8(path));
}

void TaskService::setModelId(const QString &id) {
    orchestrator_.set_model_id(qtrans::app::to_utf8(id));
}

void TaskService::initializeBackend() {
    orchestrator_.initialize_backend();
}

QString TaskService::activeBackendLabel() const {
    return qtrans::app::from_utf8(orchestrator_.active_backend_label());
}

void TaskService::setRemoteSpec(const QString &spec) {
    orchestrator_.set_remote_spec(qtrans::app::to_utf8(spec));
}

void TaskService::setModelscopeRemoteSpec(const QString &spec) {
    orchestrator_.set_modelscope_remote_spec(qtrans::app::to_utf8(spec));
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
    const TranslatePipelinePayload &payload,
    TaskPriority priority) {
    const TaskId id = orchestrator_.submit_translate_pipeline(payload, priority);
    scheduleProcessNext();
    return id;
}

TaskId TaskService::submitBatchTranslate(const TranslatePipelinePayload &payload) {
    const TaskId id = orchestrator_.submit_translate_pipeline(payload, TaskPriority::Background);
    emit batchTaskStarted(id.value);
    scheduleProcessNext();
    return id;
}

bool TaskService::cancel(TaskId id) {
    return orchestrator_.cancel(id);
}

bool TaskService::preemptBatchTask() {
    return orchestrator_.preempt_running_background();
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
    const QString &source,
    const QString &target_language,
    const QString &source_language,
    bool back_translate,
    bool wordselect) {
    TranslatePipelinePayload payload{};
    payload.source = qtrans::app::to_utf8(source);
    payload.target_language = qtrans::app::to_utf8(target_language);
    payload.source_language = qtrans::app::to_utf8(source_language);
    payload.back_translate = back_translate;
    payload.wordselect = wordselect;
    const TaskId task_id = submitTranslatePipeline(payload, TaskPriority::Interactive);
    qtrans::log::get(qtrans::log::Component::Task)
        ->debug(
            "translateInteractive task:{} src_len:{} target:'{}' back:{} wordselect:{}",
            task_id.value,
            static_cast<int>(source.size()),
            payload.target_language,
            payload.back_translate,
            payload.wordselect);
    emit translateTaskStarted(task_id.value);
}

void TaskService::processNext() {
    orchestrator_.process_next();
}
