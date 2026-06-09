#include "text/utf8.h"

#include <gtest/gtest.h>

#include <string>

namespace {

const std::string k_emoji = "hello \xF0\x9F\x98\x80";
const std::string k_chinese = "你好世界";

}  // namespace

TEST(Utf8, EmptyInput) {
    EXPECT_TRUE(qtrans::text::is_valid_utf8(""));
    EXPECT_EQ(qtrans::text::complete_prefix_length(""), 0u);
    EXPECT_EQ(qtrans::text::next_code_point_index("", 0), 0u);
}

TEST(Utf8, ValidAsciiAndUnicode) {
    EXPECT_TRUE(qtrans::text::is_valid_utf8("hello"));
    EXPECT_TRUE(qtrans::text::is_valid_utf8(k_chinese));
    EXPECT_TRUE(qtrans::text::is_valid_utf8(k_emoji));
    EXPECT_EQ(qtrans::text::complete_prefix_length(k_chinese), k_chinese.size());
}

TEST(Utf8, IncompleteSuffixIsExcluded) {
    const std::string partial = "a\xE4\xB8";
    EXPECT_FALSE(qtrans::text::is_valid_utf8(partial));
    EXPECT_EQ(qtrans::text::complete_prefix_length(partial), 1u);
}

TEST(Utf8, NextCodePointIndexStepsByCharacter) {
    EXPECT_EQ(qtrans::text::next_code_point_index(k_chinese, 0), 3u);
    EXPECT_EQ(qtrans::text::next_code_point_index(k_chinese, 3), 6u);
    EXPECT_EQ(qtrans::text::next_code_point_index(k_emoji, 6), k_emoji.size());
}

TEST(Utf8, ValidateOrThrowAcceptsValidText) {
    EXPECT_NO_THROW(qtrans::text::validate_or_throw(k_chinese));
}

TEST(Utf8, ValidateOrThrowRejectsInvalidText) {
    const std::string invalid = "\xFF";
    EXPECT_THROW(qtrans::text::validate_or_throw(invalid), std::invalid_argument);
}
