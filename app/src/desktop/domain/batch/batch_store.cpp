#include "domain/batch/batch_store.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

// ── Serialization helpers ──────────────────────────────────────────────────

// Only backslash and newline need escaping for the value content.
constexpr char kSep = '=';
constexpr char kDelim = '\n';

std::string escape(const std::string &value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        if (c == '\\') {
            out += "\\\\";
        } else if (c == '\n') {
            out += "\\n";
        } else {
            out += c;
        }
    }
    return out;
}

std::string unescape(const std::string &value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            switch (value[i + 1]) {
                case 'n':
                    out += '\n';
                    ++i;
                    break;
                case '\\':
                    out += '\\';
                    ++i;
                    break;
                default:
                    out += value[i];
                    break;
            }
        } else {
            out += value[i];
        }
    }
    return out;
}

// ── Key-value line parsing ─────────────────────────────────────────────────

struct KvLine {
    std::string key;
    std::string value;
};

std::optional<KvLine> parse_kv(const std::string &line) {
    auto eq = line.find(kSep);
    if (eq == std::string::npos) return std::nullopt;
    return KvLine{line.substr(0, eq), unescape(line.substr(eq + 1))};
}

std::string make_kv(const std::string &key, const std::string &value) {
    return key + kSep + escape(value);
}

std::string make_kv(const std::string &key, int value) {
    return key + kSep + std::to_string(value);
}

std::string make_kv(const std::string &key, std::int64_t value) {
    return key + kSep + std::to_string(value);
}

// ── Test-only failure injection ─────────────────────────────────────────────
//
// Real disks rarely fail on demand, so the I/O failure paths below are hard to
// reach from tests. These hooks let tests force each step to fail
// deterministically so the "never replace an existing queue with temp content"
// guarantee can be verified on every platform. They are compiled only when
// QTRANS_BUILD_TESTS is defined (set by CMake for test builds); production
// builds contain no hook state, setter symbols, or test branches at all.
#if defined(QTRANS_BUILD_TESTS)
struct IoFailureHooks {
    bool write = false;
    bool flush = false;
    bool close = false;
    bool sync = false;
    bool commit = false;
};

IoFailureHooks &io_failure_hooks() {
    static IoFailureHooks hooks;
    return hooks;
}
#endif  // QTRANS_BUILD_TESTS

// Best-effort removal of a temp file that failed to be committed. Errors are
// ignored because there is nothing more we can do about them (e.g. the file is
// still locked by a half-closed handle).
void remove_best_effort(const std::filesystem::path &path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// Writes `content` to `path` (binary, truncated) and returns a non-empty
// message describing the first stream failure (create, write, flush, or
// close). Returns an empty string on success. The caller must remove the file
// when a failure is reported; the stream is always closed before returning.
std::string write_file_safely(const std::filesystem::path &path,
                              const std::string &content) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return "failed to create batch queue temp file: " + path.u8string();
    }
    out << content;
#if defined(QTRANS_BUILD_TESTS)
    if (io_failure_hooks().write) out.setstate(std::ios::badbit);
#endif
    if (!out.good()) {
        return "failed to write batch queue temp file: " + path.u8string();
    }
    out.flush();
#if defined(QTRANS_BUILD_TESTS)
    if (io_failure_hooks().flush) out.setstate(std::ios::badbit);
#endif
    if (!out.good()) {
        return "failed to flush batch queue temp file: " + path.u8string();
    }
    out.close();
#if defined(QTRANS_BUILD_TESTS)
    if (io_failure_hooks().close) out.setstate(std::ios::badbit);
#endif
    if (!out.good()) {
        return "failed to close batch queue temp file: " + path.u8string();
    }
    return {};
}

// Best-effort flush of a file's contents to stable storage before it is
// atomically swapped into place, so a crash right after the swap cannot leave
// a zero-length or truncated queue behind. Returns an error_code describing
// the first failure (open, flush, or close); a cleared error_code means the
// data reached stable storage.
std::error_code sync_file_to_disk(const std::filesystem::path &path) {
#if defined(QTRANS_BUILD_TESTS)
    if (io_failure_hooks().sync) {
        return std::make_error_code(std::errc::io_error);
    }
#endif
#ifdef _WIN32
    const HANDLE handle = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                                        OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return std::error_code(::GetLastError(), std::system_category());
    }
    if (::FlushFileBuffers(handle) == FALSE) {
        const std::error_code ec(::GetLastError(), std::system_category());
        ::CloseHandle(handle);
        return ec;
    }
    if (::CloseHandle(handle) == FALSE) {
        return std::error_code(::GetLastError(), std::system_category());
    }
    return std::error_code();
#else
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        return std::error_code(errno, std::generic_category());
    }
    if (::fsync(fd) != 0) {
        const std::error_code ec(errno, std::generic_category());
        ::close(fd);
        return ec;
    }
    if (::close(fd) != 0) {
        return std::error_code(errno, std::generic_category());
    }
    return std::error_code();
#endif
}

// ── Format ─────────────────────────────────────────────────────────────────
//
// Each entry block:
//   id=<escaped>
//   path=<escaped>
//   file_type=<int>
//   source_language=<escaped>
//   target_language=<escaped>
//   entry_state=<int>
//   created_at=<epoch_sec>
//   updated_at=<epoch_sec>
//   _segments=<count>
//   s=<index>:<start>:<end>:<state>
//   s=<index>:<start>:<end>:<state>
//   <blank line>
//
// The blank line separates entries. No trailing blank line at EOF.

constexpr const char *kFieldId = "id";
constexpr const char *kFieldFormat = "qtrans_batch";
constexpr const char *kFieldPath = "path";
constexpr const char *kFieldOutputPath = "output_path";
constexpr const char *kFieldFileType = "file_type";
constexpr const char *kFieldSourceLang = "source_language";
constexpr const char *kFieldTargetLang = "target_language";
constexpr const char *kFieldEntryState = "entry_state";
constexpr const char *kFieldCreated = "created_at";
constexpr const char *kFieldUpdated = "updated_at";
constexpr const char *kFieldSegments = "_segments";
constexpr const char *kFieldTrailing = "trailing";
constexpr const char *kFieldSeg = "s";
constexpr const char *kFieldSegSource = "ss";
constexpr const char *kFieldSegText = "st";
constexpr const char *kFieldSegLiteral = "sl";
constexpr int kCurrentFormatVersion = 2;
constexpr int kMaxPersistedEntries = 10000;
constexpr int kMaxSegmentsPerEntry = 100000;

}  // namespace

// ── Test-only failure-injection API ─────────────────────────────────────────
//
// Declared directly by tests/batch/test_batch_store.cpp (no header, so the
// production API surface stays unchanged). Compiled only under
// QTRANS_BUILD_TESTS; normal production builds emit no setter symbols.
#if defined(QTRANS_BUILD_TESTS)
namespace batch_store_test {

void set_fail_write(bool enabled) {
    io_failure_hooks().write = enabled;
}
void set_fail_flush(bool enabled) {
    io_failure_hooks().flush = enabled;
}
void set_fail_close(bool enabled) {
    io_failure_hooks().close = enabled;
}
void set_fail_sync(bool enabled) {
    io_failure_hooks().sync = enabled;
}
void set_fail_commit(bool enabled) {
    io_failure_hooks().commit = enabled;
}

}  // namespace batch_store_test
#endif  // QTRANS_BUILD_TESTS

// ── BatchStore implementation ───────────────────────────────────────────────

BatchStore::BatchStore(std::filesystem::path queue_file)
    : queue_file_(std::move(queue_file)) {
}

std::vector<BatchEntry> BatchStore::load() const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Recover from an interrupted atomic write.
    std::error_code ec;
    std::filesystem::path tmp_path = queue_file_;
    tmp_path += ".tmp";

    if (std::filesystem::exists(tmp_path, ec)) {
        if (!std::filesystem::exists(queue_file_, ec)) {
            // Queue file missing but tmp exists: rename tmp to queue file.
            std::filesystem::rename(tmp_path, queue_file_, ec);
        } else {
            // Both exist; remove stale tmp.
            std::filesystem::remove(tmp_path, ec);
        }
    }

    if (!std::filesystem::exists(queue_file_, ec)) {
        return {};
    }

    std::ifstream in(queue_file_);
    if (!in) return {};

    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    return deserialize(content);
}

void BatchStore::save(const std::vector<BatchEntry> &entries) {
    std::lock_guard<std::mutex> lock(mutex_);
    atomic_write(queue_file_, serialize(entries));
}

void BatchStore::append(const BatchEntry &entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto entries = load_unlocked();
    entries.push_back(entry);
    atomic_write(queue_file_, serialize(entries));
}

void BatchStore::remove(const std::string &entry_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto entries = load_unlocked();
    entries.erase(
        std::remove_if(entries.begin(), entries.end(),
                       [&](const BatchEntry &e) { return e.id == entry_id; }),
        entries.end());
    atomic_write(queue_file_, serialize(entries));
}

void BatchStore::update_entry_state(const std::string &entry_id,
                                    BatchEntryState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto entries = load_unlocked();
    for (auto &e : entries) {
        if (e.id == entry_id) {
            e.state = state;
            e.updated_at =
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
            break;
        }
    }
    atomic_write(queue_file_, serialize(entries));
}

void BatchStore::update_segment_state(const std::string &entry_id,
                                      int segment_index,
                                      BatchSegmentState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto entries = load_unlocked();
    for (auto &e : entries) {
        if (e.id == entry_id) {
            for (auto &seg : e.file.segments) {
                if (seg.index == segment_index) {
                    seg.state = state;
                    break;
                }
            }
            e.updated_at =
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
            break;
        }
    }
    atomic_write(queue_file_, serialize(entries));
}

void BatchStore::update_segment_translated(const std::string &entry_id,
                                           int segment_index,
                                           const std::string &translated_text) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto entries = load_unlocked();
    for (auto &e : entries) {
        if (e.id == entry_id) {
            for (auto &seg : e.file.segments) {
                if (seg.index == segment_index) {
                    seg.translated_text = translated_text;
                    break;
                }
            }
            e.updated_at =
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
            break;
        }
    }
    atomic_write(queue_file_, serialize(entries));
}

bool BatchStore::reset_for_retry(const std::string &entry_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto entries = load_unlocked();
    for (auto &e : entries) {
        if (e.id != entry_id) continue;
        // Only a Failed entry may transition back to Queued; everything else
        // (queued, processing, completed, cancelled) is left untouched.
        if (e.state != BatchEntryState::Failed) return false;
        for (auto &seg : e.file.segments) {
            if (seg.state == BatchSegmentState::Failed) {
                seg.state = BatchSegmentState::Pending;
            }
        }
        e.state = BatchEntryState::Queued;
        e.updated_at =
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
        atomic_write(queue_file_, serialize(entries));
        return true;
    }
    return false;
}

std::size_t BatchStore::recover_abandoned_processing() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto entries = load_unlocked();
    std::size_t normalized = 0;
    for (auto &e : entries) {
        if (e.state != BatchEntryState::Processing) continue;
        // The run that owned this entry died; completed segments keep their
        // checkpoints (and translated text), everything else re-runs.
        e.state = BatchEntryState::Queued;
        e.updated_at =
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
        ++normalized;
    }
    if (normalized > 0) {
        atomic_write(queue_file_, serialize(entries));
    }
    return normalized;
}

std::size_t BatchStore::repair_duplicate_ids() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto entries = load_unlocked();
    std::unordered_set<std::string> seen;
    std::size_t repaired = 0;
    for (auto &e : entries) {
        if (seen.insert(e.id).second) continue;
        // Second (or later) occurrence of an id: keep the first entry as the
        // stable identity and rewrite the duplicate deterministically.
        std::string candidate = e.id + "_dup";
        int suffix = 2;
        while (seen.count(candidate) > 0) {
            candidate = e.id + "_dup" + std::to_string(suffix++);
        }
        e.id = candidate;
        seen.insert(candidate);
        ++repaired;
    }
    if (repaired > 0) {
        atomic_write(queue_file_, serialize(entries));
    }
    return repaired;
}

std::filesystem::path BatchStore::quarantine_corrupt() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::error_code error;
    if (!std::filesystem::exists(queue_file_, error)) return {};
    auto quarantined = queue_file_;
    quarantined += ".corrupt." + std::to_string(
                                     std::chrono::duration_cast<std::chrono::seconds>(
                                         std::chrono::system_clock::now()
                                             .time_since_epoch())
                                         .count());
    std::filesystem::rename(queue_file_, quarantined, error);
    if (error) {
        throw std::runtime_error("failed to quarantine corrupt batch queue: " +
                                 error.message());
    }
    return quarantined;
}

// ── Private helpers ─────────────────────────────────────────────────────────

std::vector<BatchEntry> BatchStore::load_unlocked() {
    if (!std::filesystem::exists(queue_file_)) return {};
    std::ifstream in(queue_file_);
    if (!in) return {};
    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    return deserialize(content);
}

void BatchStore::atomic_write(const std::filesystem::path &path,
                              const std::string &content) {
    std::filesystem::path tmp = path;
    tmp += ".tmp";

    // 1. Write the full contents to a temp file next to the queue. Any stream
    //    failure (create/write/flush/close) aborts here, before the existing
    //    queue could be touched, and the failed temp file is removed.
    const std::string write_error = write_file_safely(tmp, content);
    if (!write_error.empty()) {
        remove_best_effort(tmp);
        throw std::runtime_error(write_error);
    }

    // 2. Make the temp file durable before swapping it into place so a crash
    //    right after the swap cannot leave a zero-length or truncated queue
    //    behind. A sync failure also aborts before the queue is touched.
    const std::error_code sync_error = sync_file_to_disk(tmp);
    if (sync_error) {
        remove_best_effort(tmp);
        throw std::runtime_error("failed to sync batch queue temp file: " +
                                 tmp.u8string() + " (" + sync_error.message() +
                                 ")");
    }

    // 3. Atomically swap the temp file into place.
    std::error_code commit_error;
#if defined(QTRANS_BUILD_TESTS)
    if (io_failure_hooks().commit) {
        commit_error = std::make_error_code(std::errc::io_error);
    } else
#endif
    {
#ifdef _WIN32
        // std::filesystem::rename cannot reliably replace an existing file on
        // Windows. ReplaceFileW is the documented atomic-replace primitive but
        // requires the destination to already exist, so first writes use a
        // plain move.
        std::error_code dest_ec;
        const bool dest_exists = std::filesystem::exists(path, dest_ec);
        bool replaced = false;
        if (!dest_ec && dest_exists) {
            replaced = ::ReplaceFileW(path.c_str(), tmp.c_str(), nullptr,
                                      REPLACEFILE_WRITE_THROUGH |
                                          REPLACEFILE_IGNORE_MERGE_ERRORS,
                                      nullptr, nullptr) != FALSE;
        } else if (dest_ec) {
            // Could not inspect the destination; fall back to move-with-replace.
            replaced = ::MoveFileExW(tmp.c_str(), path.c_str(),
                                     MOVEFILE_REPLACE_EXISTING |
                                         MOVEFILE_WRITE_THROUGH) != FALSE;
        } else {
            replaced = ::MoveFileExW(tmp.c_str(), path.c_str(),
                                     MOVEFILE_WRITE_THROUGH) != FALSE;
        }
        if (!replaced) {
            commit_error =
                std::error_code(::GetLastError(), std::system_category());
        }
#else
        std::filesystem::rename(tmp, path, commit_error);
#endif
    }
    if (commit_error) {
        // The temp file was not consumed by the failed swap; drop it so the
        // next write starts fresh and load() is not confused by a stale temp.
        remove_best_effort(tmp);
        throw std::runtime_error("failed to commit batch queue: " +
                                 path.u8string() + " (" +
                                 commit_error.message() + ")");
    }
}

// ── Serializer ──────────────────────────────────────────────────────────────

std::string BatchStore::serialize(const std::vector<BatchEntry> &entries) {
    std::ostringstream out;
    out << make_kv(kFieldFormat, kCurrentFormatVersion) << kDelim;
    for (std::size_t ei = 0; ei < entries.size(); ++ei) {
        if (ei == 0) out << kDelim;
        const auto &e = entries[ei];
        out << make_kv(kFieldId, e.id) << kDelim;
        out << make_kv(kFieldPath, e.file.path.u8string()) << kDelim;
        if (!e.output_path.empty()) {
            out << make_kv(kFieldOutputPath, e.output_path.u8string()) << kDelim;
        }
        out << make_kv(kFieldFileType, static_cast<int>(e.file.file_type))
            << kDelim;
        out << make_kv(kFieldSourceLang, e.source_language) << kDelim;
        out << make_kv(kFieldTargetLang, e.target_language) << kDelim;
        out << make_kv(kFieldEntryState, static_cast<int>(e.state)) << kDelim;
        out << make_kv(kFieldCreated, e.created_at) << kDelim;
        out << make_kv(kFieldUpdated, e.updated_at) << kDelim;
        out << make_kv(kFieldSegments,
                       static_cast<int>(e.file.segments.size()))
            << kDelim;
        if (!e.file.trailing_text.empty()) {
            out << make_kv(kFieldTrailing, e.file.trailing_text) << kDelim;
        }
        for (const auto &seg : e.file.segments) {
            out << kFieldSeg << kSep << seg.index << ':' << seg.start_line
                << ':' << seg.end_line << ':' << static_cast<int>(seg.state)
                << kDelim;
            if (!seg.source_text.empty()) {
                out << make_kv(kFieldSegSource, seg.source_text) << kDelim;
            }
            if (!seg.translated_text.empty()) {
                out << make_kv(kFieldSegText, seg.translated_text) << kDelim;
            }
            if (!seg.literal_prefix.empty()) {
                out << make_kv(kFieldSegLiteral, seg.literal_prefix) << kDelim;
            }
        }
        // Trailing blank line between entries (but not after last)
        if (ei + 1 < entries.size()) {
            out << kDelim;
        }
    }
    return out.str();
}

std::vector<BatchEntry> BatchStore::deserialize(const std::string &content) {
    std::vector<BatchEntry> entries;
    std::istringstream in(content);
    std::string line;

    BatchEntry current;
    bool in_entry = false;
    int expected_segments = -1;
    int segments_read = 0;

    auto flush_entry = [&] {
        if (in_entry && !current.id.empty()) {
            if (expected_segments < 0 ||
                static_cast<int>(current.file.segments.size()) !=
                    expected_segments) {
                throw std::runtime_error("invalid batch queue segment count");
            }
            if (entries.size() >= kMaxPersistedEntries) {
                throw std::runtime_error("batch queue entry limit exceeded");
            }
            entries.push_back(std::move(current));
        }
        current = BatchEntry{};
        in_entry = false;
        expected_segments = -1;
        segments_read = 0;
    };

    while (std::getline(in, line)) {
        // Skip blank lines (entry separator)
        if (line.empty()) {
            flush_entry();
            continue;
        }

        auto kv = parse_kv(line);
        if (!kv) continue;

        const std::string &key = kv->key;
        const std::string &value = kv->value;

        if (key == kFieldFormat && !in_entry) {
            const int version = std::stoi(value);
            if (version < 1 || version > kCurrentFormatVersion) {
                throw std::runtime_error("unsupported batch queue format version");
            }
        } else if (key == kFieldId) {
            flush_entry();
            current.id = value;
            in_entry = true;
        } else if (key == kFieldPath && in_entry) {
            current.file.path = std::filesystem::u8path(value);
        } else if (key == kFieldOutputPath && in_entry) {
            current.output_path = std::filesystem::u8path(value);
        } else if (key == kFieldFileType && in_entry) {
            current.file.file_type = static_cast<BatchFileType>(std::stoi(value));
        } else if (key == kFieldSourceLang && in_entry) {
            current.source_language = value;
        } else if (key == kFieldTargetLang && in_entry) {
            current.target_language = value;
        } else if (key == kFieldEntryState && in_entry) {
            current.state = static_cast<BatchEntryState>(std::stoi(value));
        } else if (key == kFieldCreated && in_entry) {
            current.created_at = std::stoll(value);
        } else if (key == kFieldUpdated && in_entry) {
            current.updated_at = std::stoll(value);
        } else if (key == kFieldSegments && in_entry) {
            expected_segments = std::stoi(value);
            if (expected_segments < 0 ||
                expected_segments > kMaxSegmentsPerEntry) {
                throw std::runtime_error("invalid batch queue segment count");
            }
            current.file.segments.reserve(
                static_cast<std::size_t>(expected_segments));
        } else if (key == kFieldTrailing && in_entry) {
            current.file.trailing_text = value;
        } else if (key == kFieldSeg && in_entry) {
            // Format: index:start:end:state
            std::istringstream seg_in(value);
            std::string token;
            BatchSegment seg;
            int field_idx = 0;
            while (std::getline(seg_in, token, ':')) {
                switch (field_idx++) {
                    case 0:
                        seg.index = std::stoi(token);
                        break;
                    case 1:
                        seg.start_line = std::stoi(token);
                        break;
                    case 2:
                        seg.end_line = std::stoi(token);
                        break;
                    case 3:
                        seg.state =
                            static_cast<BatchSegmentState>(std::stoi(token));
                        break;
                }
            }
            current.file.segments.push_back(std::move(seg));
            ++segments_read;
        } else if (key == kFieldSegSource && in_entry && !current.file.segments.empty()) {
            current.file.segments.back().source_text = value;
        } else if (key == kFieldSegText && in_entry && !current.file.segments.empty()) {
            // Translated text for the most recently added segment.
            current.file.segments.back().translated_text = value;
        } else if (key == kFieldSegLiteral && in_entry &&
                   !current.file.segments.empty()) {
            current.file.segments.back().literal_prefix = value;
        }
    }
    flush_entry();

    return entries;
}
