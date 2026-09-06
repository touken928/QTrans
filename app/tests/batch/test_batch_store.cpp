#include "domain/batch/batch_store.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

// Test-only failure-injection seam implemented in
// src/desktop/domain/batch/batch_store.cpp (no header, to keep the production
// API surface unchanged). The setters are inert in production; tests toggle
// them to force each I/O step of the atomic write to fail deterministically.
namespace batch_store_test {
void set_fail_write(bool enabled);
void set_fail_flush(bool enabled);
void set_fail_close(bool enabled);
void set_fail_sync(bool enabled);
void set_fail_commit(bool enabled);
}  // namespace batch_store_test

namespace {

int current_pid() {
#ifdef _WIN32
    return ::_getpid();
#else
    return ::getpid();
#endif
}

BatchEntry make_entry(const std::string &id) {
    BatchEntry entry;
    entry.id = id;
    entry.file.path = std::filesystem::path("docs") / (id + ".txt");
    entry.file.file_type = BatchFileType::Txt;
    entry.source_language = "English";
    entry.target_language = "Chinese";
    entry.state = BatchEntryState::Queued;
    entry.created_at = 1000;
    entry.updated_at = 2000;

    BatchSegment seg0;
    seg0.index = 0;
    seg0.start_line = 0;
    seg0.end_line = 5;
    seg0.source_text = "Hello world";
    seg0.translated_text = "你好，世界";
    seg0.literal_prefix = "1\r\n00:00:00 --> 00:00:01\r\n";
    seg0.state = BatchSegmentState::Completed;

    BatchSegment seg1;
    seg1.index = 1;
    seg1.start_line = 5;
    seg1.end_line = 10;
    seg1.source_text = "Second\nsegment";
    seg1.state = BatchSegmentState::Pending;

    entry.file.segments = {seg0, seg1};
    entry.file.trailing_text = "\r\n";
    entry.output_path = std::filesystem::path("batch/output") /
                        (id + "_translated.txt");
    return entry;
}

void expect_entry_eq(const BatchEntry &actual, const BatchEntry &expected) {
    EXPECT_EQ(actual.id, expected.id);
    EXPECT_EQ(actual.file.path, expected.file.path);
    EXPECT_EQ(actual.file.file_type, expected.file.file_type);
    EXPECT_EQ(actual.source_language, expected.source_language);
    EXPECT_EQ(actual.target_language, expected.target_language);
    EXPECT_EQ(actual.state, expected.state);
    EXPECT_EQ(actual.created_at, expected.created_at);
    EXPECT_EQ(actual.updated_at, expected.updated_at);
    ASSERT_EQ(actual.file.segments.size(), expected.file.segments.size());
    for (std::size_t i = 0; i < expected.file.segments.size(); ++i) {
        SCOPED_TRACE("segment " + std::to_string(i));
        EXPECT_EQ(actual.file.segments[i].index, expected.file.segments[i].index);
        EXPECT_EQ(actual.file.segments[i].start_line,
                  expected.file.segments[i].start_line);
        EXPECT_EQ(actual.file.segments[i].end_line,
                  expected.file.segments[i].end_line);
        EXPECT_EQ(actual.file.segments[i].source_text,
                  expected.file.segments[i].source_text);
        EXPECT_EQ(actual.file.segments[i].translated_text,
                  expected.file.segments[i].translated_text);
        EXPECT_EQ(actual.file.segments[i].literal_prefix,
                  expected.file.segments[i].literal_prefix);
        EXPECT_EQ(actual.file.segments[i].state,
                  expected.file.segments[i].state);
    }
    EXPECT_EQ(actual.file.trailing_text, expected.file.trailing_text);
    EXPECT_EQ(actual.output_path, expected.output_path);
}

void expect_entries_eq(const std::vector<BatchEntry> &actual,
                       const std::vector<BatchEntry> &expected) {
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        SCOPED_TRACE("entry " + std::to_string(i));
        expect_entry_eq(actual[i], expected[i]);
    }
}

class BatchStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        reset_failure_hooks();
        dir_ = std::filesystem::temp_directory_path() /
               ("qtrans_batch_store_" + std::to_string(current_pid()) + "_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
        queue_ = dir_ / "queue.bq";
    }

    void TearDown() override {
        reset_failure_hooks();
        std::filesystem::remove_all(dir_);
    }

    // Saves `original`, then tries to replace it while the given failure hook
    // is enabled. Verifies the failed save throws, reports the failing step,
    // leaves the previously committed queue byte-for-byte intact, and removes
    // the failed temp file.
    void expect_failed_save_preserves_queue(void (*set_fail)(bool),
                                            const char *message_fragment) {
        BatchStore store(queue_);
        std::vector<BatchEntry> original = {make_entry("a")};
        store.save(original);

        set_fail(true);
        std::string error_message;
        try {
            store.save({make_entry("b")});
            FAIL() << "expected save() to throw when the I/O step fails";
        } catch (const std::runtime_error &e) {
            error_message = e.what();
        }
        set_fail(false);

        expect_entries_eq(store.load(), original);
        EXPECT_FALSE(std::filesystem::exists(tmp_path()));
        EXPECT_NE(error_message.find(message_fragment), std::string::npos)
            << "unexpected error message: " << error_message;
    }

    void reset_failure_hooks() {
        batch_store_test::set_fail_write(false);
        batch_store_test::set_fail_flush(false);
        batch_store_test::set_fail_close(false);
        batch_store_test::set_fail_sync(false);
        batch_store_test::set_fail_commit(false);
    }

    std::filesystem::path tmp_path() const {
        std::filesystem::path tmp = queue_;
        tmp += ".tmp";
        return tmp;
    }

    std::filesystem::path dir_;
    std::filesystem::path queue_;
};

}  // namespace

TEST_F(BatchStoreTest, LoadMissingQueueReturnsEmpty) {
    BatchStore store(queue_);
    EXPECT_TRUE(store.load().empty());
}

TEST_F(BatchStoreTest, SaveAndLoadRoundTrip) {
    BatchStore store(queue_);
    std::vector<BatchEntry> entries = {make_entry("a"), make_entry("b")};
    store.save(entries);

    expect_entries_eq(store.load(), entries);
}

TEST_F(BatchStoreTest, LoadsVersionOneQueueForStartupMigration) {
    {
        std::ofstream queue(queue_);
        queue << "id=legacy\n"
                 "path=docs/legacy.srt\n"
                 "file_type=3\n"
                 "source_language=Auto\n"
                 "target_language=Chinese\n"
                 "entry_state=0\n"
                 "created_at=1\n"
                 "updated_at=1\n"
                 "_segments=1\n"
                 "s=0:0:1:0\n"
                 "ss=hello\n";
    }
    BatchStore store(queue_);
    const auto entries = store.load();
    ASSERT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0].id, "legacy");
    EXPECT_TRUE(entries[0].output_path.empty());
    EXPECT_TRUE(entries[0].file.segments[0].literal_prefix.empty());
}

TEST_F(BatchStoreTest, SaveReplacesExistingQueue) {
    BatchStore store(queue_);
    store.save({make_entry("a")});
    store.save({make_entry("b"), make_entry("c")});

    expect_entries_eq(store.load(), {make_entry("b"), make_entry("c")});
    EXPECT_FALSE(std::filesystem::exists(tmp_path()));
}

TEST_F(BatchStoreTest, SaveLeavesNoTempFileBehind) {
    BatchStore store(queue_);
    store.save({make_entry("a")});
    EXPECT_FALSE(std::filesystem::exists(tmp_path()));
}

TEST_F(BatchStoreTest, UnicodePathAndTextRoundTrip) {
    BatchStore store(queue_);
    BatchEntry entry;
    entry.id = "unicode-1";
    entry.file.path = std::filesystem::u8path(
        "数据/日本語ドキュメント/файл — документ/🎯 результат.txt");
    entry.file.file_type = BatchFileType::Srt;
    entry.source_language = "日本語";
    entry.target_language = "Русский";
    entry.state = BatchEntryState::Completed;
    entry.created_at = 1;
    entry.updated_at = 2;

    BatchSegment seg;
    seg.index = 0;
    seg.start_line = 1;
    seg.end_line = 2;
    seg.source_text = "こんにちは、世界！\n第二行 \\ 反斜杠\nemoji 🎉";
    seg.translated_text = "Здравствуй, мир!\nПривет 👋";
    seg.state = BatchSegmentState::Completed;
    entry.file.segments = {seg};

    store.save({entry});
    auto loaded = store.load();
    ASSERT_EQ(loaded.size(), 1u);
    expect_entry_eq(loaded[0], entry);
}

TEST_F(BatchStoreTest, BackslashPathRoundTrip) {
    BatchStore store(queue_);
    BatchEntry entry = make_entry("win");
    entry.file.path =
        std::filesystem::u8path("C:\\Users\\测试\\docs\\文件.txt");

    store.save({entry});
    auto loaded = store.load();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].file.path, entry.file.path);
}

TEST_F(BatchStoreTest, AppendPersistsEntry) {
    BatchStore store(queue_);
    store.append(make_entry("a"));
    store.append(make_entry("b"));

    expect_entries_eq(store.load(), {make_entry("a"), make_entry("b")});
}

TEST_F(BatchStoreTest, RemoveById) {
    BatchStore store(queue_);
    store.append(make_entry("a"));
    store.append(make_entry("b"));
    store.remove("a");

    expect_entries_eq(store.load(), {make_entry("b")});
}

TEST_F(BatchStoreTest, UpdateEntryState) {
    BatchStore store(queue_);
    store.append(make_entry("a"));
    store.update_entry_state("a", BatchEntryState::Completed);

    auto loaded = store.load();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].state, BatchEntryState::Completed);
    EXPECT_GT(loaded[0].updated_at, make_entry("a").updated_at);
}

TEST_F(BatchStoreTest, UpdateSegmentStateAndTranslated) {
    BatchStore store(queue_);
    store.append(make_entry("a"));
    store.update_segment_state("a", 1, BatchSegmentState::Completed);
    store.update_segment_translated("a", 1, "译文");

    auto loaded = store.load();
    ASSERT_EQ(loaded.size(), 1u);
    ASSERT_EQ(loaded[0].file.segments.size(), 2u);
    EXPECT_EQ(loaded[0].file.segments[1].state, BatchSegmentState::Completed);
    EXPECT_EQ(loaded[0].file.segments[1].translated_text, "译文");
}

TEST_F(BatchStoreTest, RecoversTempFileWhenQueueMissing) {
    BatchStore store(queue_);
    std::vector<BatchEntry> entries = {make_entry("a"), make_entry("b")};
    store.save(entries);

    // Simulate a crash between the temp-file write and the atomic swap.
    std::error_code ec;
    std::filesystem::rename(queue_, tmp_path(), ec);
    ASSERT_FALSE(ec) << ec.message();
    ASSERT_TRUE(std::filesystem::exists(tmp_path()));
    ASSERT_FALSE(std::filesystem::exists(queue_));

    auto recovered = store.load();
    expect_entries_eq(recovered, entries);
    EXPECT_TRUE(std::filesystem::exists(queue_));
    EXPECT_FALSE(std::filesystem::exists(tmp_path()));
}

TEST_F(BatchStoreTest, DiscardsStaleTempFileWhenQueueExists) {
    BatchStore store(queue_);
    std::vector<BatchEntry> entries = {make_entry("a")};
    store.save(entries);

    {
        std::ofstream stale(tmp_path());
        stale << "stale garbage";
    }
    ASSERT_TRUE(std::filesystem::exists(tmp_path()));

    auto loaded = store.load();
    expect_entries_eq(loaded, entries);
    EXPECT_FALSE(std::filesystem::exists(tmp_path()));
}

// The core guarantee: a queue that was successfully committed must never be
// replaced with temp content when any step of the atomic write fails.

TEST_F(BatchStoreTest, FailedWritePreservesExistingQueue) {
    expect_failed_save_preserves_queue(batch_store_test::set_fail_write, "write");
}

TEST_F(BatchStoreTest, FailedFlushPreservesExistingQueue) {
    expect_failed_save_preserves_queue(batch_store_test::set_fail_flush, "flush");
}

TEST_F(BatchStoreTest, FailedClosePreservesExistingQueue) {
    expect_failed_save_preserves_queue(batch_store_test::set_fail_close, "close");
}

TEST_F(BatchStoreTest, FailedSyncPreservesExistingQueue) {
    expect_failed_save_preserves_queue(batch_store_test::set_fail_sync, "sync");
}

TEST_F(BatchStoreTest, FailedCommitPreservesExistingQueue) {
    expect_failed_save_preserves_queue(batch_store_test::set_fail_commit, "commit");
}

TEST_F(BatchStoreTest, FailedWriteOnMissingQueueLeavesNothingBehind) {
    BatchStore store(queue_);
    batch_store_test::set_fail_write(true);
    EXPECT_THROW(store.save({make_entry("a")}), std::runtime_error);
    batch_store_test::set_fail_write(false);

    EXPECT_FALSE(std::filesystem::exists(queue_));
    EXPECT_FALSE(std::filesystem::exists(tmp_path()));
}

TEST_F(BatchStoreTest, FailedFlushDuringAppendPreservesQueue) {
    BatchStore store(queue_);
    store.append(make_entry("a"));

    batch_store_test::set_fail_flush(true);
    EXPECT_THROW(store.append(make_entry("b")), std::runtime_error);
    batch_store_test::set_fail_flush(false);

    expect_entries_eq(store.load(), {make_entry("a")});
    EXPECT_FALSE(std::filesystem::exists(tmp_path()));
}

// ── Reset-for-retry ──────────────────────────────────────────────────────────

TEST_F(BatchStoreTest, ResetForRetryTransitionsFailedEntryBackToQueued) {
    BatchStore store(queue_);
    BatchEntry entry = make_entry("a");
    entry.state = BatchEntryState::Failed;
    entry.file.segments[1].state = BatchSegmentState::Failed;
    store.append(entry);

    EXPECT_TRUE(store.reset_for_retry("a"));

    const auto loaded = store.load();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].state, BatchEntryState::Queued);
    ASSERT_EQ(loaded[0].file.segments.size(), 2u);
    // Completed segments keep their translated text and state; the failed
    // segment returns to Pending so the batch re-attempts it.
    EXPECT_EQ(loaded[0].file.segments[0].state, BatchSegmentState::Completed);
    EXPECT_EQ(loaded[0].file.segments[0].translated_text, "你好，世界");
    EXPECT_EQ(loaded[0].file.segments[1].state, BatchSegmentState::Pending);
    EXPECT_GT(loaded[0].updated_at, entry.updated_at);
}

TEST_F(BatchStoreTest, ResetForRetryIsNoOpForNonFailedEntries) {
    BatchStore store(queue_);
    BatchEntry queued = make_entry("q");
    queued.state = BatchEntryState::Queued;
    BatchEntry completed = make_entry("c");
    completed.state = BatchEntryState::Completed;
    store.save({queued, completed});

    EXPECT_FALSE(store.reset_for_retry("q"));
    EXPECT_FALSE(store.reset_for_retry("c"));

    auto loaded = store.load();
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].state, BatchEntryState::Queued);
    EXPECT_EQ(loaded[1].state, BatchEntryState::Completed);
}

TEST_F(BatchStoreTest, ResetForRetryUnknownIdIsNoOp) {
    BatchStore store(queue_);
    BatchEntry entry = make_entry("a");
    entry.state = BatchEntryState::Failed;
    store.append(entry);

    EXPECT_FALSE(store.reset_for_retry("missing"));
    const auto loaded = store.load();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].state, BatchEntryState::Failed);
}

// ── Startup recovery: abandoned Processing entries ───────────────────────────

TEST_F(BatchStoreTest, RecoverAbandonedProcessingNormalizesToQueued) {
    BatchStore store(queue_);
    BatchEntry processing = make_entry("interrupted");
    processing.state = BatchEntryState::Processing;
    store.append(processing);

    EXPECT_EQ(store.recover_abandoned_processing(), 1u);

    const auto loaded = store.load();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].state, BatchEntryState::Queued);
    // Completed segment checkpoints (and translated text) survive the reset.
    ASSERT_EQ(loaded[0].file.segments.size(), 2u);
    EXPECT_EQ(loaded[0].file.segments[0].state, BatchSegmentState::Completed);
    EXPECT_EQ(loaded[0].file.segments[0].translated_text, "你好，世界");
    EXPECT_EQ(loaded[0].file.segments[1].state, BatchSegmentState::Pending);
}

TEST_F(BatchStoreTest, RecoverAbandonedProcessingLeavesOtherStatesAlone) {
    BatchStore store(queue_);
    BatchEntry queued = make_entry("q");
    queued.state = BatchEntryState::Queued;
    BatchEntry completed = make_entry("c");
    completed.state = BatchEntryState::Completed;
    BatchEntry failed = make_entry("f");
    failed.state = BatchEntryState::Failed;
    store.save({queued, completed, failed});

    EXPECT_EQ(store.recover_abandoned_processing(), 0u);

    const auto loaded = store.load();
    ASSERT_EQ(loaded.size(), 3u);
    EXPECT_EQ(loaded[0].state, BatchEntryState::Queued);
    EXPECT_EQ(loaded[1].state, BatchEntryState::Completed);
    EXPECT_EQ(loaded[2].state, BatchEntryState::Failed);
}

// ── Startup recovery: duplicate entry ids ────────────────────────────────────

TEST_F(BatchStoreTest, RepairDuplicateIdsRewritesLaterOccurrences) {
    BatchStore store(queue_);
    BatchEntry first = make_entry("dup");
    BatchEntry second = make_entry("dup");
    second.file.path = std::filesystem::path("docs") / "other.txt";
    BatchEntry third = make_entry("dup");
    third.file.path = std::filesystem::path("docs") / "third.txt";
    store.save({first, second, third});

    EXPECT_EQ(store.repair_duplicate_ids(), 2u);

    const auto loaded = store.load();
    ASSERT_EQ(loaded.size(), 3u);
    // The first occurrence keeps its stable id; later ones become unique.
    EXPECT_EQ(loaded[0].id, "dup");
    EXPECT_NE(loaded[1].id, "dup");
    EXPECT_NE(loaded[2].id, "dup");
    EXPECT_NE(loaded[1].id, loaded[2].id);
    // Queue order is preserved.
    EXPECT_EQ(loaded[0].file.path, first.file.path);
    EXPECT_EQ(loaded[1].file.path, second.file.path);
    EXPECT_EQ(loaded[2].file.path, third.file.path);
}

TEST_F(BatchStoreTest, RepairDuplicateIdsIsNoOpForUniqueQueues) {
    BatchStore store(queue_);
    store.save({make_entry("a"), make_entry("b")});

    EXPECT_EQ(store.repair_duplicate_ids(), 0u);

    const auto loaded = store.load();
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].id, "a");
    EXPECT_EQ(loaded[1].id, "b");
}

TEST_F(BatchStoreTest, RepairDuplicateIdsIsIdempotent) {
    BatchStore store(queue_);
    store.save({make_entry("dup"), make_entry("dup")});

    EXPECT_EQ(store.repair_duplicate_ids(), 1u);
    // A second pass finds nothing left to repair.
    EXPECT_EQ(store.repair_duplicate_ids(), 0u);

    const auto loaded = store.load();
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_NE(loaded[0].id, loaded[1].id);
}
