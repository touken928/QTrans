#include <gtest/gtest.h>

#include "qtrans/core/prompt_formatter.h"

using namespace qtrans::core;

TEST(PromptFormatter, UnconfiguredByDefault) {
    PromptFormatter formatter;
    EXPECT_FALSE(formatter.is_configured());
}

TEST(PromptFormatter, FormatsWhenConfigured) {
    PromptFormatter formatter;
    formatter.build_user_prompt = [](const std::string &text, const std::string &) {
        return "user:" + text;
    };
    formatter.format_chat_prompt = [](const std::string &user_prompt) {
        return "chat:" + user_prompt;
    };
    ASSERT_TRUE(formatter.is_configured());
    EXPECT_EQ(formatter.format_translation_prompt("hi", "English"), "chat:user:hi");
}
