#include "local_model_test_access.h"

#include <gtest/gtest.h>

#include <string>

namespace {

const char kHyBos[] = u8"<\xEF\xBD\x9Chy_begin\xe2\x96\x81of\xe2\x96\x81sentence\xEF\xBD\x9C>";
const char kHyUser[] = u8"<\xEF\xBD\x9Chy_User\xEF\xBD\x9C>";
const char kHyAssistant[] = u8"<\xEF\xBD\x9Chy_Assistant\xEF\xBD\x9C>";

}  // namespace

TEST(LocalModelContainsChinese, AsciiIsNotChinese) {
    EXPECT_FALSE(LocalModelTestAccess::contains_chinese("hello world"));
    EXPECT_FALSE(LocalModelTestAccess::contains_chinese(""));
    EXPECT_FALSE(LocalModelTestAccess::contains_chinese("12345 !@#"));
}

TEST(LocalModelContainsChinese, CjkIdeographsAreChinese) {
    EXPECT_TRUE(LocalModelTestAccess::contains_chinese("你好"));
    EXPECT_TRUE(LocalModelTestAccess::contains_chinese("hello 中文 world"));
    EXPECT_TRUE(LocalModelTestAccess::contains_chinese("繁體字"));
}

TEST(LocalModelContainsChinese, JapaneseKanaIsNotChinese) {
    // Hiragana range is 0x3040-0x309F; should be skipped by the CJK Unified
    // Ideographs check (0x4E00-0x9FFF).
    EXPECT_FALSE(LocalModelTestAccess::contains_chinese("こんにちは"));
}

TEST(LocalModelBuildUserPrompt, AutoBranchDoesNotAddLanguage) {
    const std::string p = LocalModelTestAccess::build_user_prompt("Hello there.", "Auto");
    EXPECT_EQ(p, "Translate the following segment:\n\nHello there.");
}

TEST(LocalModelBuildUserPrompt, EnglishSourceAndEnglishTargetUsesEnglishTemplate) {
    const std::string p = LocalModelTestAccess::build_user_prompt("Hello there.", "English");
    EXPECT_EQ(p, "Translate the following segment into English, without additional explanation.\n\nHello there.");
}

TEST(LocalModelBuildUserPrompt, ChineseSourceWithNonChineseTargetUsesChineseTemplate) {
    const std::string p = LocalModelTestAccess::build_user_prompt("你好世界", "English");
    EXPECT_EQ(p, "将以下文本翻译为英语，注意只需要输出翻译后的结果，不要额外解释：\n\n你好世界");
}

TEST(LocalModelBuildUserPrompt, ChineseSourceWithChineseTargetUsesEnglishTemplate) {
    const std::string p = LocalModelTestAccess::build_user_prompt("你好世界", "Chinese");
    EXPECT_EQ(
        p,
        "Translate the following segment into Chinese, without additional explanation.\n\n你好世界");
}

TEST(LocalModelBuildUserPrompt, NonChineseSourceWithNonChineseTargetUsesEnglishTemplate) {
    const std::string p = LocalModelTestAccess::build_user_prompt("Bonjour", "Japanese");
    EXPECT_EQ(
        p,
        "Translate the following segment into Japanese, without additional explanation.\n\nBonjour");
}

TEST(LocalModelFormatChatPrompt, WrapsWithBosUserAssistantTokens) {
    const std::string out = LocalModelTestAccess::format_chat_prompt("USER_TEXT");
    EXPECT_EQ(out, std::string(kHyBos) + kHyUser + "USER_TEXT" + kHyAssistant);
}

TEST(LocalModelFormatChatPrompt, EmptyUserPromptStillWraps) {
    const std::string out = LocalModelTestAccess::format_chat_prompt("");
    EXPECT_EQ(out, std::string(kHyBos) + kHyUser + kHyAssistant);
}
