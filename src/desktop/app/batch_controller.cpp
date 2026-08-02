#include "app/batch_controller.h"

#include "shared/string_bridge.h"
#include "domain/batch/batch_file_handler.h"
#include "domain/batch/batch_store.h"
#include "domain/logging/component.h"
#include "domain/logging/logger.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <utility>

// Boundary guard tail for Qt-invokable/slot methods: exceptions must never
// escape into the Qt event loop. `__func__` names the failing method.
#define QTRANS_BATCH_BOUNDARY(stop_batch)                         \
    catch (const std::exception &error) {                         \
        handleBoundaryError((stop_batch), __func__, error);       \
    }                                                             \
    catch (...) {                                                 \
        handleBoundaryError((stop_batch), __func__,               \
                            std::runtime_error("unknown error")); \
    }

// Same guard for const query methods, which return a safe default instead.
#define QTRANS_BATCH_QUERY_BOUNDARY(default_return)                          \
    catch (const std::exception &error) {                                    \
        log_boundary_failure(__func__, error);                               \
        return default_return;                                               \
    }                                                                        \
    catch (...) {                                                            \
        log_boundary_failure(__func__, std::runtime_error("unknown error")); \
        return default_return;                                               \
    }

namespace {

constexpr int kRequeueDelayMs = 1500;

std::int64_t now_epoch_sec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::int64_t now_epoch_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
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

void log_boundary_failure(const char *operation, const std::exception &error) {
    qtrans::log::get(qtrans::log::Component::App)
        ->error("batch {} failed: {}", operation, error.what());
}

}  // namespace

BatchController::BatchController(InferenceService *inferenceService,
                                 std::filesystem::path queueFilePath,
                                 std::filesystem::path outputDir,
                                 QObject *parent)
    : QObject(parent),
      inferenceService_(inferenceService),
      store_(std::move(queueFilePath)),
      outputDir_(std::move(outputDir)) {
    requeueTimer_.setSingleShot(true);

    connect(inferenceService_, &InferenceService::translationStarted,
            this, &BatchController::onTranslationStarted);
    connect(inferenceService_, &InferenceService::translationDelta,
            this, &BatchController::onTranslationDelta);
    connect(inferenceService_, &InferenceService::translationFinished,
            this, &BatchController::onTranslationFinished);
    connect(&requeueTimer_, &QTimer::timeout,
            this, &BatchController::onRequeueTimer);
}

// ── Initialisation ──────────────────────────────────────────────────────────

void BatchController::loadPersistedEntries() {
    try {
        // Startup recovery: an interrupted run can leave entries in
        // Processing (normalize back to Queued, keeping completed segment
        // checkpoints) or duplicate ids (rewrite to unique ids). Both are
        // repaired here, before any UI projection is emitted.
        store_.recover_abandoned_processing();
        store_.repair_duplicate_ids();

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
                                    QString::fromStdString(path.u8string()));
                }
            }
        }
        // One complete projection so a table UI can populate without
        // per-entry round trips.
        emitQueueSnapshot();
    }
    QTRANS_BATCH_BOUNDARY(false)
}

// ── File management ─────────────────────────────────────────────────────────

void BatchController::addFile(const QString &path,
                              const QString &source_lang,
                              const QString &target_lang) {
    try {
        const auto native_path = std::filesystem::u8path(qtrans::app::to_utf8(path));
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
        // Millisecond timestamp plus a monotonic per-controller suffix makes
        // the durable id collision-resistant: two files with the same stem
        // enqueued within one second (or even one tick) can never collide.
        entry.id = native_path.stem().u8string() + "_" +
                   std::to_string(now_epoch_ms()) + "_" + std::to_string(++id_seq_);
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
        emitQueueSnapshot();
    }
    QTRANS_BATCH_BOUNDARY(false)
}

void BatchController::removeEntry(const QString &entry_id) {
    try {
        const std::string id = qtrans::app::to_utf8(entry_id);
        const bool was_active = currentEntryId_ == id;
        if (was_active && currentJobId_.is_valid()) {
            inferenceService_->cancel(currentJobId_);
            // Keep currentJobId_ so the cancelled job's terminal event is still
            // matched; removedActiveEntry_ consumes it and only then advances,
            // so a new batch job is never submitted while the old invocation is
            // still completing.
            removedActiveEntry_ = true;
            currentSegmentIndex_ = -1;
            currentOutputText_.clear();
            currentErrorMessage_.clear();
        }
        if (was_active) {
            currentEntryId_.clear();
        }
        store_.remove(id);
        emit entryRemoved(entry_id);
        emitQueueSnapshot();
        if (was_active && running_ && !paused_ && !removedActiveEntry_) {
            // No in-flight job to wait for (e.g. entry removed after a
            // preempted/failed run): continue with the remaining queue now.
            advanceBatch();
        } else {
            emitBatchState();
        }
    }
    QTRANS_BATCH_BOUNDARY(true)
}

// ── Lifecycle control ───────────────────────────────────────────────────────

void BatchController::start() {
    try {
        if (running_) return;
        if (!inferenceService_->isModelLoaded()) {
            emit errorOccurred(QStringLiteral("Load a model before starting batch translation"));
            return;
        }
        running_ = true;
        paused_ = false;
        emitBatchState();
        advanceBatch();
    }
    QTRANS_BATCH_BOUNDARY(true)
}

void BatchController::pause() {
    try {
        if (!running_ || paused_) return;
        paused_ = true;
        if (currentJobId_.is_valid()) {
            inferenceService_->preemptBatch();
        }
        emitBatchState();
    }
    QTRANS_BATCH_BOUNDARY(true)
}

void BatchController::resume() {
    try {
        if (!running_ || !paused_) return;
        if (!inferenceService_->isModelLoaded()) {
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
    QTRANS_BATCH_BOUNDARY(true)
}

// ── Save / export ───────────────────────────────────────────────────────────

void BatchController::saveEntry(const QString &entry_id) {
    try {
        const auto entries = store_.load();
        const std::string id = qtrans::app::to_utf8(entry_id);
        auto *entry = find_entry(const_cast<std::vector<BatchEntry> &>(entries), id);
        if (!entry) return;
        if (entry->state != BatchEntryState::Completed) return;

        writeOutputFile(*entry);
        const auto path = output_path_for(entry->file.path, outputDir_);
        emit entrySaved(entry_id, QString::fromStdString(path.u8string()));
        emitQueueSnapshot();
    }
    QTRANS_BATCH_BOUNDARY(false)
}

void BatchController::saveEntriesToDirectory(const QStringList &entry_ids,
                                             const QString &dest_dir) {
    try {
        const std::filesystem::path dir =
            std::filesystem::u8path(qtrans::app::to_utf8(dest_dir));
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            qtrans::log::get(qtrans::log::Component::App)
                ->error("batch cannot create save dir: {} ({})",
                        dir.u8string(), ec.message());
            emit errorOccurred(QStringLiteral("Cannot create save directory"));
            return;
        }

        const auto entries = store_.load();
        for (const auto &eid : entry_ids) {
            const std::string id = qtrans::app::to_utf8(eid);
            const auto *entry = find_entry(entries, id);
            if (!entry || entry->state != BatchEntryState::Completed) continue;

            const auto out_path = output_path_for(entry->file.path, dir);
            auto *handler = get_handler(entry->file.file_type);
            if (!handler) continue;

            const std::string text = handler->assembleOutput(entry->file.segments);
            std::ofstream out(out_path, std::ios::binary);
            if (!out) continue;
            out << text;
            out.flush();

            qtrans::log::get(qtrans::log::Component::App)
                ->debug("batch saved to: {} ({} bytes)", out_path.u8string(), text.size());
            emit entrySaved(eid, QString::fromStdString(out_path.u8string()));
        }
        emitQueueSnapshot();
    }
    QTRANS_BATCH_BOUNDARY(false)
}

// ── Retry ───────────────────────────────────────────────────────────────────

void BatchController::retryEntry(const QString &entry_id) {
    try {
        const std::string id = qtrans::app::to_utf8(entry_id);
        if (store_.reset_for_retry(id)) {
            emit entryStateChanged(entry_id, static_cast<int>(BatchEntryState::Queued));
            emitQueueSnapshot();
            return;
        }
        emit errorOccurred(QStringLiteral(
            "Entry cannot be retried: only failed entries can be re-run"));
    }
    QTRANS_BATCH_BOUNDARY(false)
}

// ── Query ───────────────────────────────────────────────────────────────────

QStringList BatchController::entryIds() const {
    try {
        const auto entries = store_.load();
        QStringList ids;
        ids.reserve(static_cast<int>(entries.size()));
        for (const auto &e : entries) {
            ids.append(QString::fromStdString(e.id));
        }
        return ids;
    }
    QTRANS_BATCH_QUERY_BOUNDARY(QStringList{})
}

QString BatchController::entryFileName(const QString &entry_id) const {
    try {
        const auto entries = store_.load();
        const auto *e = find_entry(entries, qtrans::app::to_utf8(entry_id));
        return e ? QString::fromStdString(e->file.path.filename().u8string())
                 : QString{};
    }
    QTRANS_BATCH_QUERY_BOUNDARY(QString{})
}

QString BatchController::entryFilePath(const QString &entry_id) const {
    try {
        const auto entries = store_.load();
        const auto *e = find_entry(entries, qtrans::app::to_utf8(entry_id));
        return e ? QString::fromStdString(e->file.path.u8string()) : QString{};
    }
    QTRANS_BATCH_QUERY_BOUNDARY(QString{})
}

QString BatchController::entrySourceLanguage(const QString &entry_id) const {
    try {
        const auto entries = store_.load();
        const auto *e = find_entry(entries, qtrans::app::to_utf8(entry_id));
        return e ? QString::fromStdString(e->source_language) : QString{};
    }
    QTRANS_BATCH_QUERY_BOUNDARY(QString{})
}

QString BatchController::entryTargetLanguage(const QString &entry_id) const {
    try {
        const auto entries = store_.load();
        const auto *e = find_entry(entries, qtrans::app::to_utf8(entry_id));
        return e ? QString::fromStdString(e->target_language) : QString{};
    }
    QTRANS_BATCH_QUERY_BOUNDARY(QString{})
}

int BatchController::entryState(const QString &entry_id) const {
    try {
        const auto entries = store_.load();
        const auto *e = find_entry(entries, qtrans::app::to_utf8(entry_id));
        return e ? static_cast<int>(e->state) : -1;
    }
    QTRANS_BATCH_QUERY_BOUNDARY(-1)
}

int BatchController::entryProgress(const QString &entry_id) const {
    try {
        const auto entries = store_.load();
        const auto *e = find_entry(entries, qtrans::app::to_utf8(entry_id));
        if (!e) return 0;
        int done = 0;
        for (const auto &seg : e->file.segments) {
            if (seg.state == BatchSegmentState::Completed) ++done;
        }
        return done;
    }
    QTRANS_BATCH_QUERY_BOUNDARY(0)
}

int BatchController::entryTotal(const QString &entry_id) const {
    try {
        const auto entries = store_.load();
        const auto *e = find_entry(entries, qtrans::app::to_utf8(entry_id));
        return e ? static_cast<int>(e->file.segments.size()) : 0;
    }
    QTRANS_BATCH_QUERY_BOUNDARY(0)
}

QVariantMap BatchController::entryMetadata(const QString &entry_id) const {
    QVariantMap m;
    try {
        const auto entries = store_.load();
        const auto *e = find_entry(entries, qtrans::app::to_utf8(entry_id));
        if (!e) return m;
        return snapshotEntry(*e);
    }
    QTRANS_BATCH_QUERY_BOUNDARY(m)
}

QVariantMap BatchController::snapshotEntry(const BatchEntry &entry) const {
    QVariantMap m;
    m.insert(QStringLiteral("id"), QString::fromStdString(entry.id));
    m.insert(QStringLiteral("file"),
             QString::fromStdString(entry.file.path.filename().u8string()));
    m.insert(QStringLiteral("file_path"),
             QString::fromStdString(entry.file.path.u8string()));
    m.insert(QStringLiteral("source"), QString::fromStdString(entry.source_language));
    m.insert(QStringLiteral("target"), QString::fromStdString(entry.target_language));
    m.insert(QStringLiteral("state"), static_cast<int>(entry.state));

    int done = 0;
    for (const auto &seg : entry.file.segments) {
        if (seg.state == BatchSegmentState::Completed) ++done;
    }
    m.insert(QStringLiteral("segments_done"), done);
    m.insert(QStringLiteral("segments_total"), static_cast<int>(entry.file.segments.size()));

    const bool completed = (entry.state == BatchEntryState::Completed);
    m.insert(QStringLiteral("completed"), completed);

    if (completed) {
        const auto path = output_path_for(entry.file.path, outputDir_);
        const bool exists = std::filesystem::exists(path);
        m.insert(QStringLiteral("saved"), exists);
        m.insert(QStringLiteral("save_path"),
                 exists ? QString::fromStdString(path.u8string()) : QString{});
    } else {
        m.insert(QStringLiteral("saved"), false);
        m.insert(QStringLiteral("save_path"), QString{});
    }

    return m;
}

void BatchController::emitQueueSnapshot() {
    try {
        const auto entries = store_.load();
        QVariantList list;
        list.reserve(static_cast<int>(entries.size()));
        for (const auto &e : entries) {
            list.append(snapshotEntry(e));
        }
        emit queueSnapshot(list);
    }
    QTRANS_BATCH_BOUNDARY(false)
}

// ── Private slots ───────────────────────────────────────────────────────────

void BatchController::onTranslationStarted(TranslationJobId job_id) {
    try {
        if (!currentJobId_.is_valid() || job_id != currentJobId_) return;
        currentOutputText_.clear();
    }
    QTRANS_BATCH_BOUNDARY(true)
}

void BatchController::onTranslationDelta(TranslationJobId job_id,
                                         TranslationChannel channel,
                                         const QString &piece) {
    try {
        if (!currentJobId_.is_valid() || job_id != currentJobId_) return;
        if (channel != TranslationChannel::Target) return;
        currentOutputText_ += piece;
    }
    QTRANS_BATCH_BOUNDARY(true)
}

void BatchController::onTranslationFinished(const TranslationJobResult &result) {
    try {
        if (!currentJobId_.is_valid() || result.id != currentJobId_) return;

        if (removedActiveEntry_) {
            // Terminal event for the job of an entry removed while active. The
            // entry is gone so nothing is state-processed; consuming this event
            // is what releases the queue to submit the next item.
            removedActiveEntry_ = false;
            currentJobId_ = TranslationJobId{};
            currentOutputText_.clear();
            emitQueueSnapshot();
            advanceBatch();
            return;
        }

        if (!running_) return;

        const TranslationState state = result.state;

        auto entries = store_.load();
        bool found = false;
        for (auto &entry : entries) {
            if (entry.id != currentEntryId_) continue;
            found = true;

            if (state == TranslationState::Completed) {
                for (auto &seg : entry.file.segments) {
                    if (seg.index == currentSegmentIndex_) {
                        seg.state = BatchSegmentState::Completed;
                        seg.translated_text = currentOutputText_.toStdString();
                        break;
                    }
                }
            } else if (state == TranslationState::Failed) {
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
            if (state == TranslationState::Completed) {
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
            // Project the segment checkpoint immediately; the terminal
            // snapshot at the end of this slot covers state transitions.
            emitQueueSnapshot();
        }

        const QString error_message = result.error_message.empty()
                                          ? QString{}
                                          : QString::fromStdString(result.error_message);
        currentJobId_ = TranslationJobId{};
        currentOutputText_.clear();

        if (state == TranslationState::Completed) {
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
                                        QString::fromStdString(path.u8string()));
                        break;
                    }
                }
                currentEntryId_.clear();
                currentSegmentIndex_ = -1;
            }

            if (!paused_) advanceBatch();
        } else if (state == TranslationState::Failed) {
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
        emitQueueSnapshot();
    }
    QTRANS_BATCH_BOUNDARY(true)
}

void BatchController::onRequeueTimer() {
    try {
        if (!running_ || paused_) return;
        if (currentJobId_.is_valid()) return;
        advanceBatch();
    }
    QTRANS_BATCH_BOUNDARY(true)
}

// ── Private helpers ─────────────────────────────────────────────────────────

void BatchController::advanceBatch() {
    try {
        if (!running_ || paused_) return;
        if (currentJobId_.is_valid()) return;
        if (!inferenceService_->isModelLoaded()) {
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

            // An entry owning a failed segment must never execute any of its
            // remaining Pending segments, including across restarts.
            bool has_failed_segment = false;
            for (const auto &seg : entry.file.segments) {
                if (seg.state == BatchSegmentState::Failed) {
                    has_failed_segment = true;
                    break;
                }
            }
            if (has_failed_segment) {
                if (entry.state != BatchEntryState::Failed) {
                    setEntryState(entry.id, BatchEntryState::Failed);
                    emit entryStateChanged(QString::fromStdString(entry.id),
                                           static_cast<int>(BatchEntryState::Failed));
                    emit errorOccurred(QStringLiteral(
                        "Entry skipped: a segment failed; remove and re-add the file to retry"));
                }
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

            // No pending and no failed segments left: the entry is finished.
            setEntryState(entry.id, BatchEntryState::Completed);
            emit entryStateChanged(QString::fromStdString(entry.id),
                                   static_cast<int>(BatchEntryState::Completed));
            writeOutputFile(entry);
            const auto path = output_path_for(entry.file.path, outputDir_);
            emit entrySaved(QString::fromStdString(entry.id),
                            QString::fromStdString(path.u8string()));
        }

        running_ = false;
        emitBatchState();
        emit batchFinished();
        emitQueueSnapshot();
    }
    QTRANS_BATCH_BOUNDARY(true)
}

void BatchController::submitNextSegment(const BatchEntry &entry,
                                        int segment_index) {
    if (segment_index < 0 ||
        segment_index >= static_cast<int>(entry.file.segments.size())) {
        return;
    }

    const auto &seg = entry.file.segments[segment_index];

    BatchTranslationRequest request;
    request.source = seg.source_text;
    request.target_language = entry.target_language;
    request.source_language = entry.source_language;

    currentEntryId_ = entry.id;
    currentSegmentIndex_ = segment_index;
    currentOutputText_.clear();
    currentErrorMessage_.clear();

    setEntryState(entry.id, BatchEntryState::Processing);
    emit entryStateChanged(QString::fromStdString(entry.id),
                           static_cast<int>(BatchEntryState::Processing));
    // Project the Processing transition immediately so the UI never shows a
    // stale Queued row while the segment runs.
    emitQueueSnapshot();

    currentJobId_ = inferenceService_->translateBatch(request);
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
                    outputDir_.u8string(), ec.message());
        return;
    }

    auto *handler = get_handler(entry.file.file_type);
    if (!handler) {
        qtrans::log::get(qtrans::log::Component::App)
            ->error("batch no handler for file type {}", static_cast<int>(entry.file.file_type));
        return;
    }

    const auto out_path = output_path_for(entry.file.path, outputDir_);
    const std::string text = handler->assembleOutput(entry.file.segments);

    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        qtrans::log::get(qtrans::log::Component::App)
            ->error("batch cannot write output file: {}", out_path.u8string());
        return;
    }
    out << text;
    out.flush();

    qtrans::log::get(qtrans::log::Component::App)
        ->debug("batch wrote output: {} ({} bytes)", out_path.u8string(), text.size());
}

void BatchController::handleBoundaryError(bool stop_batch, const char *operation,
                                          const std::exception &error) {
    qtrans::log::get(qtrans::log::Component::App)
        ->error("batch {} failed: {}", operation, error.what());
    if (stop_batch) {
        running_ = false;
        paused_ = false;
        removedActiveEntry_ = false;
        requeueTimer_.stop();
        if (currentJobId_.is_valid()) inferenceService_->cancel(currentJobId_);
        currentJobId_ = TranslationJobId{};
        currentEntryId_.clear();
        currentSegmentIndex_ = -1;
        currentOutputText_.clear();
        currentErrorMessage_.clear();
        emitBatchState();
    }
    emit errorOccurred(QStringLiteral("Batch %1 failed: %2")
                           .arg(QString::fromUtf8(operation),
                                QString::fromUtf8(error.what())));
}

void BatchController::emitBatchState() {
    emit batchStateChanged(running_, paused_);
}
