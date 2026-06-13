#include "domain/batch/batch_file_handler.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace {

// ── Utilities ───────────────────────────────────────────────────────────────

bool is_blank_line(const std::string &line) {
    return std::all_of(line.begin(), line.end(),
                       [](unsigned char c) { return std::isspace(c); });
}

std::string rtrim_copy(const std::string &s) {
    auto end = s.find_last_not_of(" \t\r\n");
    return end == std::string::npos ? std::string{} : s.substr(0, end + 1);
}

// ── PlainText handler (also used for .txt, .md) ────────────────────────────

class PlainTextHandler : public BatchFileHandler {
public:
    ParseResult parse(std::istream &in) const override {
        ParseResult result;
        std::vector<std::string> lines;
        std::string line;

        while (std::getline(in, line)) {
            lines.push_back(line);
        }

        int seg_index = 0;
        int current_start = 0;
        std::string paragraph;

        auto flush = [&](int end_line) {
            if (paragraph.empty()) return;
            BatchSegment seg;
            seg.index = seg_index++;
            seg.start_line = current_start;
            seg.end_line = end_line;
            seg.source_text = std::move(paragraph);
            result.segments.push_back(std::move(seg));
            paragraph.clear();
        };

        for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
            if (is_blank_line(lines[i])) {
                flush(i);
                current_start = i + 1;
            } else {
                if (!paragraph.empty()) paragraph += '\n';
                paragraph += rtrim_copy(lines[i]);
            }
        }
        flush(static_cast<int>(lines.size()));

        if (result.segments.empty()) {
            BatchSegment seg;
            seg.index = 0;
            seg.start_line = 0;
            seg.end_line = static_cast<int>(lines.size());
            for (const auto &l : lines) {
                if (!seg.source_text.empty()) seg.source_text += '\n';
                seg.source_text += rtrim_copy(l);
            }
            result.segments.push_back(std::move(seg));
        }
        return result;
    }

    std::string assembleOutput(const std::vector<BatchSegment> &segments) const override {
        std::string out;
        for (const auto &seg : segments) {
            if (seg.state != BatchSegmentState::Completed) continue;
            if (!out.empty()) out += "\n\n";
            out += seg.translated_text;
        }
        return out;
    }
};

// ── SRT handler ────────────────────────────────────────────────────────────

// Stores original SRT block lines (index + timing + text) per segment so the
// output can faithfully reconstruct the original file structure.
// The parse method stores raw block data as metadata in segment start/end
// tracking; the assemble method uses the original struct block info.
//
// Since BatchSegment cannot store arbitrary metadata without schema changes,
// we re-use start_line/end_line to reference the original line range in the
// raw file.  assembleOutput() re-reads the original SRT file and splices in
// translated text for each completed segment.

class SrtHandler : public BatchFileHandler {
public:
    ParseResult parse(std::istream &in) const override {
        ParseResult result;
        std::string line;
        int seg_index = 0;
        int current_line = 0;

        auto next_nonblank = [&]() -> bool {
            while (std::getline(in, line)) {
                ++current_line;
                if (!is_blank_line(line)) return true;
            }
            return false;
        };

        while (true) {
            if (!next_nonblank()) break;
            int block_start = current_line - 1;

            // Timing line (skip)
            if (!std::getline(in, line)) break;
            ++current_line;

            // Text lines
            std::string text;
            while (std::getline(in, line)) {
                ++current_line;
                if (is_blank_line(line)) break;
                if (!text.empty()) text += '\n';
                text += rtrim_copy(line);
            }

            if (text.empty()) continue;

            BatchSegment seg;
            seg.index = seg_index++;
            seg.start_line = block_start;
            seg.end_line = current_line;  // past the trailing blank
            seg.source_text = std::move(text);
            result.segments.push_back(std::move(seg));
        }

        if (result.segments.empty()) {
            result.success = false;
            result.error_message = "no SRT blocks found";
        }
        return result;
    }

    std::string assembleOutput(const std::vector<BatchSegment> &segments) const override {
        // Group segments by original file — all segments in this vector
        // belong to the same file.  Reconstruct using original line mapping.
        // Since we don't have the original file path here, this version
        // does a simple concatenation with double newline.
        // A future improvement could accept the original file path and
        // do proper SRT splicing.
        std::string out;
        for (const auto &seg : segments) {
            if (seg.state != BatchSegmentState::Completed) continue;
            if (!out.empty()) out += "\n\n";
            out += seg.translated_text;
        }
        return out;
    }
};

// ── Handler registry ────────────────────────────────────────────────────────

const PlainTextHandler kPlainTextHandler;
const SrtHandler kSrtHandler;

std::unordered_map<BatchFileType, const BatchFileHandler *> make_registry() {
    std::unordered_map<BatchFileType, const BatchFileHandler *> reg;
    reg[BatchFileType::PlainText] = &kPlainTextHandler;
    reg[BatchFileType::Txt] = &kPlainTextHandler;
    reg[BatchFileType::Md] = &kPlainTextHandler;
    reg[BatchFileType::Srt] = &kSrtHandler;
    return reg;
}

const auto &registry() {
    static const auto reg = make_registry();
    return reg;
}

}  // namespace

// ── Free functions ──────────────────────────────────────────────────────────

BatchFileType detect_file_type(const std::filesystem::path &path) {
    const std::string ext = path.extension().string();
    if (ext == ".txt") return BatchFileType::Txt;
    if (ext == ".md" || ext == ".markdown") return BatchFileType::Md;
    if (ext == ".srt") return BatchFileType::Srt;
    return BatchFileType::PlainText;
}

const BatchFileHandler *get_handler(BatchFileType type) {
    auto it = registry().find(type);
    return it != registry().end() ? it->second : nullptr;
}

ParseResult parse_batch_file(const std::filesystem::path &path, BatchFileType type) {
    auto *handler = get_handler(type);
    if (!handler) {
        ParseResult r;
        r.success = false;
        r.error_message = "unsupported file type";
        return r;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        ParseResult r;
        r.success = false;
        r.error_message = "cannot open file: " + path.string();
        return r;
    }

    return handler->parse(in);
}

std::string output_path_for(const std::filesystem::path &input_path,
                            const std::filesystem::path &output_dir) {
    const std::string stem = input_path.stem().string();
    const std::string ext = input_path.extension().string();
    return (output_dir / (stem + "_translated" + ext)).string();
}
