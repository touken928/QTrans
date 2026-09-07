#include "text/sentence_splitter.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace qtrans::core {
namespace {

TEST(SentenceSplitterTest, HandlesEmptyInput) {
    EXPECT_TRUE(split_sentences("").empty());
}

TEST(SentenceSplitterTest, SplitsWithoutDroppingWhitespace) {
    EXPECT_EQ(split_sentences("One. Two!\nThree?"),
              (std::vector<std::string>{"One. ", "Two!\n", "Three?"}));
}

TEST(SentenceSplitterTest, PreservesCommonAbbreviation) {
    EXPECT_EQ(split_sentences("Mr. Smith arrived."),
              (std::vector<std::string>{"Mr. Smith arrived."}));
}

TEST(SentenceSplitterTest, HandlesCjkAndCrLfBoundaries) {
    EXPECT_EQ(split_sentences("你好。世界！\r\n再见。"),
              (std::vector<std::string>{"你好。", "世界！\r\n", "再见。"}));
}

}
}  // namespace qtrans::core
