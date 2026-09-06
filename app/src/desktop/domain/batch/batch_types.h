#pragma once

#include "domain/batch/batch_enums.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct BatchSegment {
    int index = 0;
    int start_line = 0;  // 0-based line index into the original file (inclusive)
    int end_line = 0;    // 0-based line index (exclusive)
    std::string source_text;
    std::string translated_text;  // persisted for recovery/export
    // Exact non-translatable bytes preceding this segment. Together with
    // BatchFile::trailing_text this forms a lossless document IR: rendering
    // replaces only source_text and preserves format structure verbatim.
    std::string literal_prefix;
    BatchSegmentState state = BatchSegmentState::Pending;
};

struct BatchFile {
    std::filesystem::path path;
    BatchFileType file_type = BatchFileType::PlainText;
    std::vector<BatchSegment> segments;
    std::string trailing_text;
};

struct BatchEntry {
    std::string id;
    BatchFile file;
    std::string source_language;
    std::string target_language;
    BatchEntryState state = BatchEntryState::Queued;
    std::int64_t created_at = 0;  // epoch seconds
    std::int64_t updated_at = 0;  // epoch seconds
    // Stable output allocation. Empty only for queues written by older builds;
    // BatchController assigns it during startup migration.
    std::filesystem::path output_path;
};
