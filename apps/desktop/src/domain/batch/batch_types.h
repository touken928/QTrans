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
    BatchSegmentState state = BatchSegmentState::Pending;
};

struct BatchFile {
    std::filesystem::path path;
    BatchFileType file_type = BatchFileType::PlainText;
    std::vector<BatchSegment> segments;
};

struct BatchEntry {
    std::string id;
    BatchFile file;
    std::string source_language;
    std::string target_language;
    BatchEntryState state = BatchEntryState::Queued;
    std::int64_t created_at = 0;  // epoch seconds
    std::int64_t updated_at = 0;  // epoch seconds
};
