#include "app/batch_controller.h"

#include "shared/string_bridge.h"
#include "domain/batch/batch_file_handler.h"
#include "domain/batch/batch_store.h"
#include "domain/logging/component.h"
#include "domain/logging/logger.h"
#include "domain/tasks/task_types.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <utility>

namespace {

constexpr int kRequeueDelayMs = 1500;

std::int64_t now_epoch_sec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Find entry by id in a vector; returns nullptr if not found.
const BatchEntry *find_entry(const std::vector<BatchEntry> &entries,
                             const std::string &id) {
    for (const auto &e : entries) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

BatchEntry *find_entry(std::vector<BatchEntry> &entries,
                       const std::string &id) {
    for (auto &e : entries) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

}  // namespace

BatchController::BatchController(TaskService *taskService,
                                 std::filesystem::path queueFilePath,
                                 std::filesystem::path outputDir,
                                 QObject *parent)
    : QObject(parent),
      taskService_(taskService),
      store_(std::move(queueFilePath)),
      outputDir_(std::move(outputDir)) {
    requeueTimer_.setSingleShot(true);

    connect(taskService_, &TaskService::translationFinished,
            this, &BatchController::onTranslationFinished);
    connect(taskService_, &TaskService::taskFailed,
            this, &BatchController::onTaskFailed);
    connect(taskService_, &TaskService::targetReset,
            this, &BatchController::onTargetReset);
    connect(taskService_, &TaskService::targetAppended,
            this, &BatchController::onTargetAppended);
    connect(&requeueTimer_, &QTimer::timeout,
            this, &BatchController::onRequeueTimer);
}

// ── Initialisation ──────────────────────────────────────────────────────────

void BatchController::loadPersistedEntries() {
    const auto entries = store_.load();
    for (const auto &e : entries) {
        emit entryAdded(QString::fromStdString(e.id),
                        QString::fromStdString(e.source_language),
                        QString::fromStdString(e.target_language));
        emit entryStateChanged(QString::fromStdString(e.id),
                               static_cast<int>(e.state));

        int done = 0;
        for (const auto &seg : e.file.segments) {
            if (seg.state == BatchSegmentState::Completed) ++done;
        }
        emit segmentProgress(QString::fromStdString(e.id), done,
                             static_cast<int>(e.file.segments.size()));

        // If completed and output file exists, emit saved.
        if (e.state == BatchEntryState::Completed) {
            const auto path = output_path_for(e.file.path, outputDir_);
            if (std::filesystem::exists(path)) {
                emit entrySaved(QString::fromStdString(e.id),
                                QString::fromStdString(path));
            }
        }
    }
}

// ── File management ─────────────────────────────────────────────────────────

void BatchController::addFile(const QString &path,
                              const QString &source_lang,
                              const QString &target_lang) {
    const auto native_path = std::filesystem::path(qtrans::app::to_utf8(path));
    const std::string src = qtrans::app::to_utf8(source_lang);
    const std::string tgt = qtrans::app::to_utf8(target_lang);

    const BatchFileType type = detect_file_type(native_path);
    ParseResult parsed = parse_batch_file(native_path, type);
    if (!parsed.success) {
        emit errorOccurred(QString::fromStdString(parsed.error_message));
        return;
    }
    if (parsed.segments.empty()) {
        emit errorOccurred(QStringLiteral("No translatable content found in file"));
        return;
    }

    BatchEntry entry;
    entry.id = native_path.stem().string() + "_" + std::to_string(now_epoch_sec());
    entry.file.path = native_path;
    entry.file.file_type = type;
    entry.file.segments = std::move(parsed.segments);
    entry.source_language = src;
    entry.target_language = tgt;
    entry.state = BatchEntryState::Queued;
    entry.created_at = now_epoch_sec();
    entry.updated_at = entry.created_at;

    store_.append(entry);
    emit entryAdded(QString::fromStdString(entry.id), source_lang, target_lang);
    emitBatchState();
}

void BatchController::removeEntry(const QString &entry_id) {
    const std::string id = qtrans::app::to_utf8(entry_id);
    if (running_ && currentEntryId_ == id && currentTaskId_.is_valid()) {
        taskService_->cancel(currentTaskId_);
        currentTaskId_ = TaskId{};
        currentEntryId_.clear();
        currentSegmentIndex_ = -1;
        currentOutputText_.clear();
    }
    store_.remove(id);
    emit entryRemoved(entry_id);
    emitBatchState();
}

// ── Lifecycle control ───────────────────────────────────────────────────────

void BatchController::start() {
    if (running_) return;
    if (!taskService_->isModelLoaded()) {
        emit errorOccurred(QStringLiteral("Load a model before starting batch translation"));
        return;
    }
    running_ = true;
    paused_ = false;
    emitBatchState();
    advanceBatch();
}

void BatchController::pause() {
    if (!running_ || paused_) return;
    paused_ = true;
    if (currentTaskId_.is_valid()) {
        taskService_->preemptBatchTask();
    }
    emitBatchState();
}

void BatchController::resume() {
    if (!running_ || !paused_) return;
    if (!taskService_->isModelLoaded()) {
        paused_ = false;
        running_ = false;
        emitBatchState();
        emit errorOccurred(QStringLiteral("Load a model before resuming batch translation"));
        return;
    }
    paused_ = false;
    emitBatchState();
    advanceBatch();
}

// ── Save / export ───────────────────────────────────────────────────────────

void BatchController::saveEntry(const QString &entry_id) {
    const auto entries = store_.load();
    const std::string id = qtrans::app::to_utf8(entry_id);
    auto *entry = find_entry(const_cast<std::vector<BatchEntry> &>(entries), id);
    if (!entry) return;
    if (entry->state != BatchEntryState::Completed) return;

    writeOutputFile(*entry);
    const auto path = output_path_for(entry->file.path, outputDir_);
    emit entrySaved(entry_id, QString::fromStdString(path));
}

void BatchController::saveEntriesToDirectory(const QStringList &entry_ids,
                                              const QString &dest_dir) {
    const std::filesystem::path dir = qtrans::app::to_utf8(dest_dir);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        qtrans::log::get(qtrans::log::Component::App)
            ->error("batch cannot create save dir: {} ({})", dir.string(), ec.message());
        emit errorOccurred(QStringLiteral("Cannot create save directory"));
        return;
    }

    const auto entries = store_.load();
    for (const auto &eid : entry_ids) {
        const std::string id = qtrans::app::to_utf8(eid);
        const auto *entry = find_entry(entries, id);
        if (!entry || entry->state != BatchEntryState::Completed) continue;

        const std::string out_path = output_path_for(entry->file.path, dir);
        auto *handler = get_handler(entry->file.file_type);
        if (!handler) continue;

        const std::string text = handler->assembleOutput(entry->file.segments);
        std::ofstream out(out_path, std::ios::binary);
        if (!out) continue;
        out << text;
        out.flush();

        qtrans::log::get(qtrans::log::Component::App)
            ->debug("batch saved to: {} ({} bytes)", out_path, text.size());
        emit entrySaved(eid, QString::fromStdString(out_path));
    }
}

// ── Query ───────────────────────────────────────────────────────────────────

QStringList BatchController::entryIds() const {
    const auto entries = store_.load();
    QStringList ids;
    ids.reserve(static_cast<int>(entries.size()));
    for (const auto &e : entries) {
        ids.append(QString::fromStdString(e.id));
    }
    return ids;
}

QString BatchController::entryFileName(const QString &entry_id) const {
    const auto entries = store_.load();
    const auto *e = find_entry(entries, qtrans::app::to_utf8(entry_id));
    return e ? QString::fromStdString(e->file.path.filename().string()) : QString{};
}

QString BatchController::entryFilePath(const QString &entry_id) const {
    const auto entries = store_.load();
    const auto *e = find_entry(entries, qtrans::app::to_utf8(entry_id));
    return e ? QString::fromStdString(e->file.path.string()) : QString{};
}

QString BatchController::entrySourceLanguage(const QString &entry_id) const {
    const auto entries = store_.load();
    const auto *e = find_entry(entries, qtrans::app::to_utf8(entry_id));
    return e ? QString::fromStdString(e->source_language) : QString{};
}

QString BatchController::entryTargetLanguage(const QString &entry_id) const {
    const auto entries = store_.load();
    const auto *e = find_entry(entries, qtrans::app::to_utf8(entry_id));
    return e ? QString::fromStdString(e->target_language) : QString{};
}

int BatchController::entryState(const QString &entry_id) const {
    const auto entries = store_.load();
    const auto *e = find_entry(entries, qtrans::app::to_utf8(entry_id));
    return e ? static_cast<int>(e->state) : -1;
}

int BatchController::entryProgress(const QString &entry_id) const {
    const auto entries = store_.load();
    const auto *e = find_entry(entries, qtrans::app::to_utf8(entry_id));
    if (!e) return 0;
    int done = 0;
    for (const auto &seg : e->file.segments) {
        if (seg.state == BatchSegmentState::Completed) ++done;
    }
    return done;
}

int BatchController::entryTotal(const QString &entry_id) const {
    const auto entries = store_.load();
    const auto *e = find_entry(entries, qtrans::app::to_utf8(entry_id));
    return e ? static_cast<int>(e->file.segments.size()) : 0;
}

QVariantMap BatchController::entryMetadata(const QString &entry_id) const {
    QVariantMap m;
    const auto entries = store_.load();
    const auto *e = find_entry(entries, qtrans::app::to_utf8(entry_id));
    if (!e) return m;

    m.insert(QStringLiteral("id"), QString::fromStdString(e->id));
    m.insert(QStringLiteral("file"), QString::fromStdString(e->file.path.filename().string()));
    m.insert(QStringLiteral("file_path"), QString::fromStdString(e->file.path.string()));
    m.insert(QStringLiteral("source"), QString::fromStdString(e->source_language));
    m.insert(QStringLiteral("target"), QString::fromStdString(e->target_language));
    m.insert(QStringLiteral("state"), static_cast<int>(e->state));

    int done = 0;
    for (const auto &seg : e->file.segments) {
        if (seg.state == BatchSegmentState::Completed) ++done;
    }
    m.insert(QStringLiteral("segments_done"), done);
    m.insert(QStringLiteral("segments_total"), static_cast<int>(e->file.segments.size()));

    const bool completed = (e->state == BatchEntryState::Completed);
    m.insert(QStringLiteral("completed"), completed);

    if (completed) {
        const auto path = output_path_for(e->file.path, outputDir_);
        const bool exists = std::filesystem::exists(path);
        m.insert(QStringLiteral("saved"), exists);
        m.insert(QStringLiteral("save_path"), exists ? QString::fromStdString(path) : QString{});
    } else {
        m.insert(QStringLiteral("saved"), false);
        m.insert(QStringLiteral("save_path"), QString{});
    }

    return m;
}

// ── Private slots ───────────────────────────────────────────────────────────

void BatchController::onTranslationFinished(quint64 task_id, int state) {
    if (!currentTaskId_.is_valid() || task_id != currentTaskId_.value) return;
    if (!running_) return;

    const auto s = static_cast<TaskState>(state);

    auto entries = store_.load();
    bool found = false;
    for (auto &entry : entries) {
        if (entry.id != currentEntryId_) continue;
        found = true;

        if (s == TaskState::Completed) {
            for (auto &seg : entry.file.segments) {
                if (seg.index == currentSegmentIndex_) {
                    seg.state = BatchSegmentState::Completed;
                    seg.translated_text = currentOutputText_.toStdString();
                    break;
                }
            }
        } else if (s == TaskState::Failed) {
            for (auto &seg : entry.file.segments) {
                if (seg.index == currentSegmentIndex_) {
                    seg.state = BatchSegmentState::Failed;
                    break;
                }
            }
        }
        // Preempted/Cancelled: leave segment Pending (checkpointed).

        entry.updated_at = now_epoch_sec();
        break;
    }

    if (found) {
        store_.save(entries);
        if (s == TaskState::Completed) {
            store_.update_segment_translated(currentEntryId_, currentSegmentIndex_,
                                             currentOutputText_.toStdString());
        }
    }

    // Progress
    int done = 0, total = 0;
    for (const auto &entry : entries) {
        if (entry.id == currentEntryId_) {
            total = static_cast<int>(entry.file.segments.size());
            for (const auto &seg : entry.file.segments) {
                if (seg.state == BatchSegmentState::Completed) ++done;
            }
            break;
        }
    }
    if (total > 0) {
        emit segmentProgress(QString::fromStdString(currentEntryId_), done, total);
    }

    const QString error_message = currentErrorMessage_;
    currentTaskId_ = TaskId{};
    currentOutputText_.clear();

    if (s == TaskState::Completed) {
        emit entryStateChanged(QString::fromStdString(currentEntryId_),
                               static_cast<int>(BatchEntryState::Processing));

        bool all_done = true;
        for (const auto &entry : entries) {
            if (entry.id == currentEntryId_) {
                for (const auto &seg : entry.file.segments) {
                    if (seg.state != BatchSegmentState::Completed) {
                        all_done = false;
                        break;
                    }
                }
                break;
            }
        }

        if (all_done) {
            setEntryState(currentEntryId_, BatchEntryState::Completed);
            emit entryStateChanged(QString::fromStdString(currentEntryId_),
                                   static_cast<int>(BatchEntryState::Completed));

            const auto updated = store_.load();
            for (const auto &e : updated) {
                if (e.id == currentEntryId_) {
                    writeOutputFile(e);
                    const auto path = output_path_for(e.file.path, outputDir_);
                    emit entrySaved(QString::fromStdString(e.id),
                                    QString::fromStdString(path));
                    break;
                }
            }
            currentEntryId_.clear();
            currentSegmentIndex_ = -1;
        }

        if (!paused_) advanceBatch();
    } else if (s == TaskState::Failed) {
        setEntryState(currentEntryId_, BatchEntryState::Failed);
        emit entryStateChanged(QString::fromStdString(currentEntryId_),
                               static_cast<int>(BatchEntryState::Failed));
        running_ = false;
        paused_ = false;
        emitBatchState();
        emit errorOccurred(error_message.isEmpty()
                               ? QStringLiteral("Batch stopped after a segment failed")
                               : QStringLiteral("Batch stopped: ") + error_message);
    } else {
        // Preempted / Cancelled
        if (!paused_) requeueTimer_.start(kRequeueDelayMs);
    }

    currentErrorMessage_.clear();
}

void BatchController::onTaskFailed(quint64 task_id, const QString &message) {
    if (currentTaskId_.is_valid() && task_id == currentTaskId_.value) {
        currentErrorMessage_ = message;
    }
}

void BatchController::onTargetReset(quint64 task_id) {
    if (currentTaskId_.is_valid() && task_id == currentTaskId_.value) {
        currentOutputText_.clear();
    }
}

void BatchController::onTargetAppended(quint64 task_id, const QString &piece) {
    if (currentTaskId_.is_valid() && task_id == currentTaskId_.value) {
        currentOutputText_ += piece;
    }
}

void BatchController::onRequeueTimer() {
    if (!running_ || paused_) return;
    if (currentTaskId_.is_valid()) return;
    advanceBatch();
}

// ── Private helpers ─────────────────────────────────────────────────────────

void BatchController::advanceBatch() {
    if (!running_ || paused_) return;
    if (currentTaskId_.is_valid()) return;
    if (!taskService_->isModelLoaded()) {
        running_ = false;
        paused_ = false;
        emitBatchState();
        emit errorOccurred(QStringLiteral("Load a model before running batch translation"));
        return;
    }

    const auto entries = store_.load();

    for (const auto &entry : entries) {
        if (entry.state == BatchEntryState::Completed ||
            entry.state == BatchEntryState::Cancelled) {
            continue;
        }

        for (int i = 0; i < static_cast<int>(entry.file.segments.size()); ++i) {
            const auto &seg = entry.file.segments[i];
            if (seg.state == BatchSegmentState::Pending) {
                if (seg.source_text.empty()) {
                    setEntryState(entry.id, BatchEntryState::Failed);
                    emit entryStateChanged(QString::fromStdString(entry.id),
                                           static_cast<int>(BatchEntryState::Failed));
                    emit errorOccurred(QStringLiteral(
                        "Batch stopped: queue data is incomplete; remove and re-add the file"));
                    running_ = false;
                    paused_ = false;
                    emitBatchState();
                    return;
                }
                submitNextSegment(entry, i);
                return;
            }
        }

        // All segments done but entry not yet marked complete.
        if (entry.state != BatchEntryState::Completed) {
            setEntryState(entry.id, BatchEntryState::Completed);
            emit entryStateChanged(QString::fromStdString(entry.id),
                                   static_cast<int>(BatchEntryState::Completed));
            writeOutputFile(entry);
            const auto path = output_path_for(entry.file.path, outputDir_);
            emit entrySaved(QString::fromStdString(entry.id),
                            QString::fromStdString(path));
        }
    }

    running_ = false;
    emitBatchState();
    emit batchFinished();
}

void BatchController::submitNextSegment(const BatchEntry &entry,
                                        int segment_index) {
    if (segment_index < 0 ||
        segment_index >= static_cast<int>(entry.file.segments.size())) {
        return;
    }

    const auto &seg = entry.file.segments[segment_index];

    TranslatePipelinePayload payload;
    payload.source = seg.source_text;
    payload.target_language = entry.target_language;
    payload.source_language = entry.source_language;
    payload.back_translate = false;
    payload.wordselect = false;

    currentEntryId_ = entry.id;
    currentSegmentIndex_ = segment_index;
    currentOutputText_.clear();
    currentErrorMessage_.clear();

    setEntryState(entry.id, BatchEntryState::Processing);
    emit entryStateChanged(QString::fromStdString(entry.id),
                           static_cast<int>(BatchEntryState::Processing));

    currentTaskId_ = taskService_->submitBatchTranslate(payload);
}

void BatchController::setEntryState(const std::string &entry_id,
                                    BatchEntryState state) {
    store_.update_entry_state(entry_id, state);
}

void BatchController::writeOutputFile(const BatchEntry &entry) {
    if (outputDir_.empty()) return;

    std::error_code ec;
    std::filesystem::create_directories(outputDir_, ec);
    if (ec) {
        qtrans::log::get(qtrans::log::Component::App)
            ->error("batch cannot create output dir: {} ({})",
                    outputDir_.string(), ec.message());
        return;
    }

    auto *handler = get_handler(entry.file.file_type);
    if (!handler) {
        qtrans::log::get(qtrans::log::Component::App)
            ->error("batch no handler for file type {}", static_cast<int>(entry.file.file_type));
        return;
    }

    const std::string out_path = output_path_for(entry.file.path, outputDir_);
    const std::string text = handler->assembleOutput(entry.file.segments);

    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        qtrans::log::get(qtrans::log::Component::App)
            ->error("batch cannot write output file: {}", out_path);
        return;
    }
    out << text;
    out.flush();

    qtrans::log::get(qtrans::log::Component::App)
        ->debug("batch wrote output: {} ({} bytes)", out_path, text.size());
}

void BatchController::emitBatchState() {
    emit batchStateChanged(running_, paused_);
}
