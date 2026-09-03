#include "domain/batch/batch_file_handler.h"
#include "domain/batch/batch_output_writer.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace {

std::string render_completed(BatchFileType type, ParseResult parsed) {
    BatchFile file;
    file.file_type = type;
    file.segments = std::move(parsed.segments);
    file.trailing_text = std::move(parsed.trailing_text);
    for (auto &segment : file.segments) {
        segment.state = BatchSegmentState::Completed;
        segment.translated_text = "译文" + std::to_string(segment.index + 1);
    }
    return get_handler(type)->assembleOutput(file);
}

}  // namespace

TEST(BatchFileHandler, PlainTextPreservesExactDocumentStructure) {
    std::istringstream input("First line\r\nsecond line\r\n\r\n  \r\nLast\r\n");
    auto parsed = get_handler(BatchFileType::Txt)->parse(input);

    ASSERT_TRUE(parsed.success);
    ASSERT_EQ(parsed.segments.size(), 2U);
    EXPECT_EQ(parsed.segments[0].source_text, "First line\r\nsecond line");
    EXPECT_EQ(parsed.segments[1].literal_prefix, "\r\n\r\n  \r\n");
    EXPECT_EQ(render_completed(BatchFileType::Txt, std::move(parsed)),
              "译文1\r\n\r\n  \r\n译文2\r\n");
}

TEST(BatchFileHandler, SrtPreservesIndexesTimingAndCrLf) {
    std::istringstream input(
        "1\r\n00:00:01,000 --> 00:00:03,000\r\nHello\r\nworld\r\n\r\n"
        "2\r\n00:00:04,000 --> 00:00:05,000 align:start\r\nBye\r\n");
    auto parsed = get_handler(BatchFileType::Srt)->parse(input);

    ASSERT_TRUE(parsed.success) << parsed.error_message;
    ASSERT_EQ(parsed.segments.size(), 2U);
    EXPECT_EQ(parsed.segments[0].source_text, "Hello\r\nworld");
    EXPECT_EQ(render_completed(BatchFileType::Srt, std::move(parsed)),
              "1\r\n00:00:01,000 --> 00:00:03,000\r\n译文1\r\n\r\n"
              "2\r\n00:00:04,000 --> 00:00:05,000 align:start\r\n译文2\r\n");
}

TEST(BatchFileHandler, RejectsStructurallyInvalidSrt) {
    std::istringstream input("1\nnot a timing line\ntext\n");
    const auto parsed = get_handler(BatchFileType::Srt)->parse(input);
    EXPECT_FALSE(parsed.success);
    EXPECT_NE(parsed.error_message.find("timing"), std::string::npos);
}

TEST(BatchFileHandler, AllocatesCollisionFreeStableNames) {
    const auto root = std::filesystem::temp_directory_path() /
                      "qtrans_batch_output_allocation";
    std::filesystem::create_directories(root);
    const auto first = output_path_for("one/report.srt", root);
    const auto second = allocate_output_path("two/report.srt", root, {first});
    EXPECT_EQ(first.filename(), "report_translated.srt");
    EXPECT_EQ(second.filename(), "report_translated_2.srt");
}

TEST(BatchOutputWriter, FailureDoesNotReplaceExistingOutput) {
    const auto root = std::filesystem::temp_directory_path() /
                      "qtrans_batch_output_failure";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto output = root / "translated.txt";
    {
        std::ofstream existing(output);
        existing << "original";
    }

    BatchEntry entry;
    entry.file.file_type = BatchFileType::Txt;
    entry.file.segments.push_back(
        {0, 0, 1, "source", "translated", "", BatchSegmentState::Completed});
    std::filesystem::create_directory(output.string() + ".tmp");

    const auto result = write_batch_output_atomic(entry, output);
    EXPECT_FALSE(result.success);
    {
        std::ifstream existing(output);
        EXPECT_EQ(std::string(std::istreambuf_iterator<char>(existing), {}),
                  "original");
    }
    std::filesystem::remove_all(root);
}
