#include "domain/batch/batch_store.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>

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
constexpr const char *kFieldPath = "path";
constexpr const char *kFieldFileType = "file_type";
constexpr const char *kFieldSourceLang = "source_language";
constexpr const char *kFieldTargetLang = "target_language";
constexpr const char *kFieldEntryState = "entry_state";
constexpr const char *kFieldCreated = "created_at";
constexpr const char *kFieldUpdated = "updated_at";
constexpr const char *kFieldSegments = "_segments";
constexpr const char *kFieldSeg = "s";
constexpr const char *kFieldSegSource = "ss";
constexpr const char *kFieldSegText = "st";

}  // namespace

// ── BatchStore implementation ───────────────────────────────────────────────

BatchStore::BatchStore(std::filesystem::path queue_file)
    : queue_file_(std::move(queue_file)) {}

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

    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) {
            throw std::runtime_error("failed to write batch queue temp file: " +
                                     tmp.string());
        }
        out << content;
        out.flush();
    }

    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        throw std::runtime_error("failed to commit batch queue: " +
                                 path.string() + " (" + ec.message() + ")");
    }
}

// ── Serializer ──────────────────────────────────────────────────────────────

std::string BatchStore::serialize(const std::vector<BatchEntry> &entries) {
    std::ostringstream out;
    for (std::size_t ei = 0; ei < entries.size(); ++ei) {
        const auto &e = entries[ei];
        out << make_kv(kFieldId, e.id) << kDelim;
        out << make_kv(kFieldPath, e.file.path.string()) << kDelim;
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
    int expected_segments = 0;
    int segments_read = 0;

    auto flush_entry = [&] {
        if (in_entry && !current.id.empty()) {
            entries.push_back(std::move(current));
        }
        current = BatchEntry{};
        in_entry = false;
        expected_segments = 0;
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

        if (key == kFieldId) {
            flush_entry();
            current.id = value;
            in_entry = true;
        } else if (key == kFieldPath && in_entry) {
            current.file.path = value;
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
            current.file.segments.reserve(
                static_cast<std::size_t>(expected_segments));
        } else if (key == kFieldSeg && in_entry) {
            // Format: index:start:end:state
            std::istringstream seg_in(value);
            std::string token;
            BatchSegment seg;
            int field_idx = 0;
            while (std::getline(seg_in, token, ':')) {
                switch (field_idx++) {
                    case 0: seg.index = std::stoi(token); break;
                    case 1: seg.start_line = std::stoi(token); break;
                    case 2: seg.end_line = std::stoi(token); break;
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
        }
    }
    flush_entry();

    return entries;
}
