#include "domain/batch/batch_file_handler.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace {

struct LineSpan {
    std::size_t begin = 0;
    std::size_t content_end = 0;
    std::size_t end = 0;
    bool blank = true;
};

std::string read_all(std::istream &in) {
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::vector<LineSpan> line_spans(std::string_view text) {
    std::vector<LineSpan> lines;
    for (std::size_t begin = 0; begin < text.size();) {
        std::size_t content_end = text.find('\n', begin);
        std::size_t end = content_end;
        if (content_end == std::string_view::npos) {
            content_end = text.size();
            end = text.size();
        } else {
            end = content_end + 1;
            if (content_end > begin && text[content_end - 1] == '\r') --content_end;
        }

        bool blank = true;
        for (std::size_t i = begin; i < content_end; ++i) {
            if (!std::isspace(static_cast<unsigned char>(text[i]))) {
                blank = false;
                break;
            }
        }
        lines.push_back({begin, content_end, end, blank});
        begin = end;
    }
    return lines;
}

void append_segment(ParseResult &result, std::string_view document,
                    const std::vector<LineSpan> &lines, std::size_t first,
                    std::size_t last, std::size_t &cursor) {
    BatchSegment segment;
    segment.index = static_cast<int>(result.segments.size());
    segment.start_line = static_cast<int>(first);
    segment.end_line = static_cast<int>(last + 1);
    segment.literal_prefix =
        std::string(document.substr(cursor, lines[first].begin - cursor));
    segment.source_text = std::string(document.substr(
        lines[first].begin, lines[last].content_end - lines[first].begin));
    cursor = lines[last].content_end;
    result.segments.push_back(std::move(segment));
}

std::string assemble_document(const BatchFile &file) {
    std::string output;
    for (const auto &segment : file.segments) {
        output += segment.literal_prefix;
        output += segment.state == BatchSegmentState::Completed
                      ? segment.translated_text
                      : segment.source_text;
    }
    output += file.trailing_text;
    return output;
}

class PlainTextHandler final : public BatchFileHandler {
public:
    ParseResult parse(std::istream &in) const override {
        ParseResult result;
        const std::string document = read_all(in);
        const auto lines = line_spans(document);
        std::size_t cursor = 0;

        for (std::size_t i = 0; i < lines.size();) {
            if (lines[i].blank) {
                ++i;
                continue;
            }
            const std::size_t first = i;
            while (i + 1 < lines.size() && !lines[i + 1].blank) ++i;
            append_segment(result, document, lines, first, i, cursor);
            ++i;
        }
        result.trailing_text = document.substr(cursor);
        return result;
    }

    std::string assembleOutput(const BatchFile &file) const override {
        return assemble_document(file);
    }
};

class SrtHandler final : public BatchFileHandler {
public:
    ParseResult parse(std::istream &in) const override {
        ParseResult result;
        const std::string document = read_all(in);
        const auto lines = line_spans(document);
        std::size_t cursor = 0;

        for (std::size_t i = 0; i < lines.size();) {
            while (i < lines.size() && lines[i].blank) ++i;
            if (i == lines.size()) break;

            ++i;  // index line is preserved in the next literal_prefix
            if (i == lines.size()) {
                result.success = false;
                result.error_message = "SRT block is missing its timing line";
                return result;
            }
            const std::size_t timing_line = i++;
            const std::string_view timing = std::string_view(document).substr(
                lines[timing_line].begin,
                lines[timing_line].content_end - lines[timing_line].begin);
            if (timing.find("-->") == std::string_view::npos) {
                result.success = false;
                result.error_message = "invalid SRT timing line";
                return result;
            }

            const std::size_t text_first = i;
            while (i < lines.size() && !lines[i].blank) ++i;
            if (text_first == i) {
                result.success = false;
                result.error_message = "SRT block is missing subtitle text";
                return result;
            }

            append_segment(result, document, lines, text_first, i - 1, cursor);
            // append_segment keeps everything before the subtitle text,
            // including index and timing lines, in literal_prefix.
        }

        result.trailing_text = document.substr(cursor);
        if (result.segments.empty()) {
            result.success = false;
            result.error_message = "no SRT blocks found";
        }
        return result;
    }

    std::string assembleOutput(const BatchFile &file) const override {
        return assemble_document(file);
    }
};

const PlainTextHandler kPlainTextHandler;
const SrtHandler kSrtHandler;

std::unordered_map<BatchFileType, const BatchFileHandler *> make_registry() {
    return {{BatchFileType::PlainText, &kPlainTextHandler},
            {BatchFileType::Txt, &kPlainTextHandler},
            {BatchFileType::Md, &kPlainTextHandler},
            {BatchFileType::Srt, &kSrtHandler}};
}

const auto &registry() {
    static const auto handlers = make_registry();
    return handlers;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

}  // namespace

BatchFileType detect_file_type(const std::filesystem::path &path) {
    const std::string extension = lowercase(path.extension().u8string());
    if (extension == ".txt") return BatchFileType::Txt;
    if (extension == ".md" || extension == ".markdown") return BatchFileType::Md;
    if (extension == ".srt") return BatchFileType::Srt;
    return BatchFileType::PlainText;
}

const BatchFileHandler *get_handler(BatchFileType type) {
    const auto it = registry().find(type);
    return it == registry().end() ? nullptr : it->second;
}

ParseResult parse_batch_file(const std::filesystem::path &path,
                             BatchFileType type) {
    const auto *handler = get_handler(type);
    if (!handler) return {{}, {}, "unsupported file type", false};

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {{}, {}, "cannot open file: " + path.u8string(), false};
    }
    return handler->parse(input);
}

std::filesystem::path output_path_for(const std::filesystem::path &input_path,
                                      const std::filesystem::path &output_dir) {
    return output_dir / std::filesystem::u8path(
                            input_path.stem().u8string() + "_translated" +
                            input_path.extension().u8string());
}

std::filesystem::path allocate_output_path(
    const std::filesystem::path &input_path,
    const std::filesystem::path &output_dir,
    const std::vector<std::filesystem::path> &reserved_paths) {
    std::unordered_set<std::string> reserved;
    for (const auto &path : reserved_paths) {
        reserved.insert(lowercase(path.lexically_normal().u8string()));
    }

    const std::string stem = input_path.stem().u8string() + "_translated";
    const std::string extension = input_path.extension().u8string();
    for (std::size_t suffix = 1;; ++suffix) {
        const std::string name =
            stem + (suffix == 1 ? "" : "_" + std::to_string(suffix)) + extension;
        const auto candidate = output_dir / std::filesystem::u8path(name);
        std::error_code error;
        if (!std::filesystem::exists(candidate, error) &&
            reserved.count(lowercase(candidate.lexically_normal().u8string())) == 0) {
            return candidate;
        }
    }
}
