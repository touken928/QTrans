#include "hymt_test_access.h"

#include <gtest/gtest.h>

#include <string>

namespace {

const char kHyBos[] = u8"<\xEF\xBD\x9Chy_begin\xe2\x96\x81of\xe2\x96\x81sentence\xEF\xBD\x9C>";
const char kHyUser[] = u8"<\xEF\xBD\x9Chy_User\xEF\xBD\x9C>";
const char kHyAssistant[] = u8"<\xEF\xBD\x9Chy_Assistant\xEF\xBD\x9C>";

}  // namespace

TEST(HymtContainsChinese, AsciiIsNotChinese) {
    EXPECT_FALSE(HymtTestAccess::contains_chinese("hello world"));
    EXPECT_FALSE(HymtTestAccess::contains_chinese(""));
    EXPECT_FALSE(HymtTestAccess::contains_chinese("12345 !@#"));
}

TEST(HymtContainsChinese, CjkIdeographsAreChinese) {
    EXPECT_TRUE(HymtTestAccess::contains_chinese("你好"));
    EXPECT_TRUE(HymtTestAccess::contains_chinese("hello 中文 world"));
    EXPECT_TRUE(HymtTestAccess::contains_chinese("繁體字"));
}

TEST(HymtContainsChinese, JapaneseKanaIsNotChinese) {
    // Hiragana range is 0x3040-0x309F; should be skipped by the CJK Unified
    // Ideographs check (0x4E00-0x9FFF).
    EXPECT_FALSE(HymtTestAccess::contains_chinese("こんにちは"));
}

TEST(HymtBuildUserPrompt, AutoBranchDoesNotAddLanguage) {
    const std::string p = HymtTestAccess::build_user_prompt("Hello there.", "Auto");
    EXPECT_EQ(p, "Translate the following segment:\n\nHello there.");
}

TEST(HymtBuildUserPrompt, EnglishSourceAndEnglishTargetUsesEnglishTemplate) {
    const std::string p = HymtTestAccess::build_user_prompt("Hello there.", "English");
    EXPECT_EQ(p, "Translate the following segment into English, without additional explanation.\n\nHello there.");
}

TEST(HymtBuildUserPrompt, ChineseSourceWithNonChineseTargetUsesChineseTemplate) {
    const std::string p = HymtTestAccess::build_user_prompt("你好世界", "English");
    EXPECT_EQ(p, "将以下文本翻译为英语，注意只需要输出翻译后的结果，不要额外解释：\n\n你好世界");
}

TEST(HymtBuildUserPrompt, ChineseSourceWithChineseTargetUsesEnglishTemplate) {
    const std::string p = HymtTestAccess::build_user_prompt("你好世界", "Chinese");
    EXPECT_EQ(
        p,
        "Translate the following segment into Chinese, without additional explanation.\n\n你好世界");
}

TEST(HymtBuildUserPrompt, NonChineseSourceWithNonChineseTargetUsesEnglishTemplate) {
    const std::string p = HymtTestAccess::build_user_prompt("Bonjour", "Japanese");
    EXPECT_EQ(
        p,
        "Translate the following segment into Japanese, without additional explanation.\n\nBonjour");
}

TEST(HymtFormatChatPrompt, WrapsWithBosUserAssistantTokens) {
    const std::string out = HymtTestAccess::format_chat_prompt("USER_TEXT");
    EXPECT_EQ(out, std::string(kHyBos) + kHyUser + "USER_TEXT" + kHyAssistant);
}

TEST(HymtFormatChatPrompt, EmptyUserPromptStillWraps) {
    const std::string out = HymtTestAccess::format_chat_prompt("");
    EXPECT_EQ(out, std::string(kHyBos) + kHyUser + kHyAssistant);
}
