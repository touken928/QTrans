#include "translation/text_chunker.h"

#include <gtest/gtest.h>

#include <numeric>
#include <string>

namespace {

int char_token_count(const std::string &text) {
    return static_cast<int>(text.size());
}

std::string join_chunks(const std::vector<std::string> &chunks) {
    std::string out;
    for (const std::string &chunk : chunks) {
        out += chunk;
    }
    return out;
}

}  // namespace

TEST(TextChunker, SplitSentencesPreservesEnglishText) {
    const std::string text = "Hello world. How are you? Fine!";
    const std::vector<std::string> sentences = split_sentences_icu(text);
    ASSERT_GE(sentences.size(), 2u);
    EXPECT_EQ(join_chunks(sentences), text);
}

TEST(TextChunker, SplitSentencesPreservesChineseText) {
    const std::string text = "你好世界。今天天气不错！是吗？";
    const std::vector<std::string> sentences = split_sentences_icu(text);
    ASSERT_GE(sentences.size(), 2u);
    EXPECT_EQ(join_chunks(sentences), text);
}

TEST(TextChunker, ChunkShortTextReturnsSingleChunk) {
    const std::string text = "Short text.";
    const std::vector<std::string> chunks =
        chunk_text_by_token_budget(text, 100, char_token_count);
    ASSERT_EQ(chunks.size(), 1u);
    EXPECT_EQ(chunks.front(), text);
}

TEST(TextChunker, ChunkMergesSentencesUpToBudget) {
    const std::string text = "One. Two. Three. Four.";
    const std::vector<std::string> chunks =
        chunk_text_by_token_budget(text, 10, char_token_count);
    ASSERT_GT(chunks.size(), 1u);
    EXPECT_EQ(join_chunks(chunks), text);
    for (const std::string &chunk : chunks) {
        EXPECT_LE(char_token_count(chunk), 10);
    }
}

TEST(TextChunker, ChunkHardSplitsOversizedSentence) {
    const std::string text = "abcdefghijklmnopqrstuvwxyz";
    const std::vector<std::string> chunks =
        chunk_text_by_token_budget(text, 5, char_token_count);
    ASSERT_GT(chunks.size(), 1u);
    EXPECT_EQ(join_chunks(chunks), text);
    for (const std::string &chunk : chunks) {
        EXPECT_LE(char_token_count(chunk), 5);
        EXPECT_FALSE(chunk.empty());
    }
}

TEST(TextChunker, ChunkPreservesParagraphBreaks) {
    const std::string text = "Paragraph one.\n\nParagraph two.";
    const std::vector<std::string> chunks =
        chunk_text_by_token_budget(text, 18, char_token_count);
    EXPECT_EQ(join_chunks(chunks), text);
}

TEST(TextChunker, EmptyInputReturnsEmptyChunks) {
    EXPECT_TRUE(chunk_text_by_token_budget("", 10, char_token_count).empty());
    EXPECT_TRUE(split_sentences_icu("").empty());
}

TEST(TextChunker, InvalidBudgetThrows) {
    EXPECT_THROW(
        chunk_text_by_token_budget("hello", 0, char_token_count),
        std::invalid_argument);
}
