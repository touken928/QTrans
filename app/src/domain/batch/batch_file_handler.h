#pragma once

#include "domain/batch/batch_enums.h"
#include "domain/batch/batch_types.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// ── ParseResult ──────────────────────────────────────────────────────────────
// (Re-declared from batch_file_parser.h for self-contained use.)

struct ParseResult {
    std::vector<BatchSegment> segments;
    std::string trailing_text;
    std::string error_message;
    bool success = true;
};

// ── OutputResult ─────────────────────────────────────────────────────────────

struct OutputResult {
    std::string text;
    std::string error_message;
    bool success = true;
};

// ── BatchFileHandler ─────────────────────────────────────────────────────────
//
// Strategy interface: every file format implements parse + output assembly.
// The factory get_handler() returns the right strategy for a given type.
// Handlers are stateless and thread-compatible.

class BatchFileHandler {
public:
    virtual ~BatchFileHandler() = default;

    // Parse an input stream into segments.
    virtual ParseResult parse(std::istream &in) const = 0;

    // Assemble completed segments back into a single output string in the
    // target format (e.g. joining paragraphs for text, reconstructing SRT
    // blocks with timing lines).
    virtual std::string assembleOutput(const BatchFile &file) const = 0;

protected:
    BatchFileHandler() = default;
};

// ── Factory ─────────────────────────────────────────────────────────────────

// Detect file type from extension (kept from batch_file_parser).
BatchFileType detect_file_type(const std::filesystem::path &path);

// Get the handler for a given file type. Returns nullptr for Unknown.
const BatchFileHandler *get_handler(BatchFileType type);

// Parse convenience that opens the file and delegates to the handler.
ParseResult parse_batch_file(const std::filesystem::path &path, BatchFileType type);

// Compute the default output path inside output_dir. Returned as a
// std::filesystem::path so it can be opened with the native, Unicode-safe
// representation on Windows.
std::filesystem::path output_path_for(const std::filesystem::path &input_path,
                                      const std::filesystem::path &output_dir);

// Allocate a collision-free output path, considering both existing files and
// paths already reserved by durable queue entries.
std::filesystem::path allocate_output_path(
    const std::filesystem::path &input_path,
    const std::filesystem::path &output_dir,
    const std::vector<std::filesystem::path> &reserved_paths);
