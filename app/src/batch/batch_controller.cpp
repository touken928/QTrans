#include "batch/batch_controller.h"

#include "shared/string_bridge.h"
#include "batch/batch_file_handler.h"
#include "batch/batch_output_writer.h"
#include "batch/batch_store.h"
#include "logging/component.h"
#include "logging/logger.h"

#include <algorithm>
#include <cctype>
#include <chrono>
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

std::string output_path_key(const std::filesystem::path &path) {
    std::string key = path.lexically_normal().u8string();
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return key;
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

BatchController::BatchController(InferenceService *inferenceService,
                                 std::filesystem::path queueFilePath,
                                 std::filesystem::path outputDir,
                                 QObject *parent)
    : QObject(parent),
      inferenceService_(inferenceService),
      store_(std::move(queueFilePath)),
      outputDir_(std::move(outputDir)) {
    // The projection is delivered across the worker -> UI thread boundary by
    // queueSnapshot, so its value type must be a registered metatype.
    qRegisterMetaType<BatchEntryView>();
    qRegisterMetaType<QVector<BatchEntryView>>();

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
        try {
            store_.recover_abandoned_processing();
            store_.repair_duplicate_ids();
        } catch (const std::exception &error) {
            const auto quarantined = store_.quarantine_corrupt();
            qtrans::log::get(qtrans::log::Component::App)
                ->error("quarantined corrupt batch queue at {}: {}",
                        quarantined.u8string(), error.what());
            emit errorOccurred(QStringLiteral(
                "Batch queue was corrupt and has been moved aside"));
            emitQueueSnapshot();
            return;
        }

        auto migrated_entries = store_.load();
        bool migrated = false;
        std::vector<std::filesystem::path> reserved_paths;
        for (auto &entry : migrated_entries) {
            if (entry.output_path.empty()) {
                const auto legacy_path =
                    output_path_for(entry.file.path, outputDir_);
                const bool already_reserved = std::any_of(
                    reserved_paths.begin(), reserved_paths.end(),
                    [&](const auto &path) {
                        return output_path_key(path) ==
                               output_path_key(legacy_path);
                    });
                entry.output_path =
                    already_reserved
                        ? allocate_output_path(entry.file.path, outputDir_,
                                               reserved_paths)
                        : legacy_path;
                migrated = true;
            }
            reserved_paths.push_back(entry.output_path);
        }
        if (migrated) store_.save(migrated_entries);

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
        entry.file.trailing_text = std::move(parsed.trailing_text);
        entry.source_language = src;
        entry.target_language = tgt;
        entry.state = BatchEntryState::Queued;
        entry.created_at = now_epoch_sec();
        entry.updated_at = entry.created_at;

        std::vector<std::filesystem::path> reserved_paths;
        for (const auto &queued : store_.load()) {
            reserved_paths.push_back(outputPath(queued));
        }
        entry.output_path =
            allocate_output_path(native_path, outputDir_, reserved_paths);

        store_.append(entry);
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
        auto entries = store_.load();
        const std::string id = qtrans::app::to_utf8(entry_id);
        auto *entry = find_entry(entries, id);
        if (!entry) return;
        if (entry->state != BatchEntryState::Completed) return;

        const auto result = writeOutputFile(*entry);
        if (!result.success) {
            emit errorOccurred(QString::fromStdString(result.error_message));
            return;
        }
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

            const auto filename = outputPath(*entry).filename();
            const auto result = write_batch_output_atomic(*entry, dir / filename);
            if (!result.success) {
                emit errorOccurred(QString::fromStdString(result.error_message));
                continue;
            }
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
            emitQueueSnapshot();
            return;
        }
        emit errorOccurred(QStringLiteral(
            "Entry cannot be retried: only failed entries can be re-run"));
    }
    QTRANS_BATCH_BOUNDARY(false)
}

BatchEntryView BatchController::snapshotEntry(const BatchEntry &entry) const {
    BatchEntryView view;
    view.id = QString::fromStdString(entry.id);
    view.file = QString::fromStdString(entry.file.path.filename().u8string());
    view.file_path = QString::fromStdString(entry.file.path.u8string());
    view.source = QString::fromStdString(entry.source_language);
    view.target = QString::fromStdString(entry.target_language);
    view.state = static_cast<int>(entry.state);

    int done = 0;
    for (const auto &seg : entry.file.segments) {
        if (seg.state == BatchSegmentState::Completed) ++done;
    }
    view.segments_done = done;
    view.segments_total = static_cast<int>(entry.file.segments.size());
    view.completed = (entry.state == BatchEntryState::Completed);
    return view;
}

QVector<BatchEntryView> BatchController::buildSnapshot(
    const std::vector<BatchEntry> &entries, const std::string &saved_entry_id,
    const std::filesystem::path &saved_path) const {
    QVector<BatchEntryView> list;
    list.reserve(static_cast<int>(entries.size()));
    for (const auto &entry : entries) {
        BatchEntryView view = snapshotEntry(entry);
        if (view.completed) {
            if (!saved_entry_id.empty() && entry.id == saved_entry_id) {
                // The caller just wrote this output; trust the write result
                // instead of probing the same path again.
                view.saved = true;
                view.save_path = QString::fromStdString(saved_path.u8string());
            } else {
                const auto path = outputPath(entry);
                const bool exists = std::filesystem::exists(path);
                view.saved = exists;
                view.save_path =
                    exists ? QString::fromStdString(path.u8string()) : QString{};
            }
        }
        list.append(view);
    }
    return list;
}

void BatchController::emitQueueSnapshot() {
    try {
        emit queueSnapshot(buildSnapshot(store_.load()));
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
            emit queueSnapshot(buildSnapshot(store_.load()));
            advanceBatch();
            return;
        }

        if (!running_) return;

        const TranslationState state = result.state;

        // Mutate the durable queue in memory, then persist it with a single
        // atomic save so the translated text and, when the outcome is known,
        // the terminal entry state are always durable together.
        auto entries = store_.load();
        const std::string entry_id = currentEntryId_;
        bool found = false;
        bool mutated = false;
        bool entry_terminal = false;
        bool output_written = false;
        std::filesystem::path written_path;
        QString write_error;

        for (auto &entry : entries) {
            if (entry.id != entry_id) continue;
            found = true;

            if (state == TranslationState::Completed) {
                for (auto &seg : entry.file.segments) {
                    if (seg.index == currentSegmentIndex_) {
                        seg.state = BatchSegmentState::Completed;
                        seg.translated_text = currentOutputText_.toStdString();
                        break;
                    }
                }
                mutated = true;

                bool all_done = true;
                for (const auto &seg : entry.file.segments) {
                    if (seg.state != BatchSegmentState::Completed) {
                        all_done = false;
                        break;
                    }
                }
                if (all_done) {
                    // Commit the output before the save so a crash afterwards
                    // still observes both the translated text and the terminal
                    // entry state.
                    const auto write_result = writeOutputFile(entry);
                    entry_terminal = true;
                    if (!write_result.success) {
                        entry.state = BatchEntryState::Failed;
                        write_error =
                            QString::fromStdString(write_result.error_message);
                    } else {
                        entry.state = BatchEntryState::Completed;
                        output_written = true;
                        written_path = write_result.path;
                    }
                }
            } else if (state == TranslationState::Failed) {
                for (auto &seg : entry.file.segments) {
                    if (seg.index == currentSegmentIndex_) {
                        seg.state = BatchSegmentState::Failed;
                        break;
                    }
                }
                entry.state = BatchEntryState::Failed;
                mutated = true;
                entry_terminal = true;
            }
            // Preempted/Cancelled: leave the segment Pending (checkpointed) and
            // write nothing.

            if (mutated) entry.updated_at = now_epoch_sec();
            break;
        }

        if (found && mutated) store_.save(entries);

        const QString error_message = result.error_message.empty()
                                          ? QString{}
                                          : QString::fromStdString(result.error_message);
        currentJobId_ = TranslationJobId{};
        currentOutputText_.clear();

        if (state == TranslationState::Completed) {
            if (entry_terminal && !output_written) {
                running_ = false;
                paused_ = false;
                emitBatchState();
                emit errorOccurred(QStringLiteral("Batch output failed: ") + write_error);
                emit queueSnapshot(buildSnapshot(entries));
                currentEntryId_.clear();
                currentSegmentIndex_ = -1;
                currentErrorMessage_.clear();
                return;
            }
            if (entry_terminal) {
                emit queueSnapshot(buildSnapshot(entries, entry_id, written_path));
                currentEntryId_.clear();
                currentSegmentIndex_ = -1;
                if (!paused_) advanceBatch();
                currentErrorMessage_.clear();
                return;
            }
            emit queueSnapshot(buildSnapshot(entries));
            if (!paused_) advanceBatch();
            currentErrorMessage_.clear();
            return;
        }

        if (state == TranslationState::Failed) {
            running_ = false;
            paused_ = false;
            emitBatchState();
            emit errorOccurred(error_message.isEmpty()
                                   ? QStringLiteral("Batch stopped after a segment failed")
                                   : QStringLiteral("Batch stopped: ") + error_message);
            emit queueSnapshot(buildSnapshot(entries));
            currentErrorMessage_.clear();
            return;
        }

        // Preempted / Cancelled
        if (!paused_) requeueTimer_.start(kRequeueDelayMs);
        emit queueSnapshot(buildSnapshot(entries));
        currentErrorMessage_.clear();
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

            // Commit the output before exposing Completed so durable state
            // remains truthful if creation, writing, or rename fails.
            const auto write_result = writeOutputFile(entry);
            if (!write_result.success) {
                setEntryState(entry.id, BatchEntryState::Failed);
                running_ = false;
                paused_ = false;
                emitBatchState();
                emit errorOccurred(QStringLiteral("Batch output failed: ") +
                                   QString::fromStdString(
                                       write_result.error_message));
                emitQueueSnapshot();
                return;
            }
            setEntryState(entry.id, BatchEntryState::Completed);
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
    // Project the Processing transition immediately so the UI never shows a
    // stale Queued row while the segment runs.
    emitQueueSnapshot();

    currentJobId_ = inferenceService_->translateBatch(request);
}

void BatchController::setEntryState(const std::string &entry_id,
                                    BatchEntryState state) {
    store_.update_entry_state(entry_id, state);
}

BatchOutputWriteResult BatchController::writeOutputFile(
    const BatchEntry &entry) {
    if (outputDir_.empty()) {
        return {false, {}, "batch output directory is empty"};
    }
    return write_batch_output_atomic(entry, outputPath(entry));
}

std::filesystem::path BatchController::outputPath(
    const BatchEntry &entry) const {
    return entry.output_path.empty()
               ? output_path_for(entry.file.path, outputDir_)
               : entry.output_path;
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
