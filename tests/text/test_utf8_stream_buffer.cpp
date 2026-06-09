#include "text/utf8_stream_buffer.h"

#include <gtest/gtest.h>

#include <string>

TEST(Utf8StreamBuffer, EmitsAsciiImmediately) {
    qtrans::text::Utf8StreamBuffer buffer;
    EXPECT_EQ(buffer.push("hello"), "hello");
    EXPECT_TRUE(buffer.flush().empty());
}

TEST(Utf8StreamBuffer, BuffersIncompleteUtf8Bytes) {
    qtrans::text::Utf8StreamBuffer buffer;
    EXPECT_TRUE(buffer.push("\xE4").empty());
    EXPECT_TRUE(buffer.push("\xB8").empty());
    EXPECT_EQ(buffer.push("\xAD"), u8"\u4E2D");
    EXPECT_TRUE(buffer.flush().empty());
}

TEST(Utf8StreamBuffer, SplitAcrossMultipleChunks) {
    qtrans::text::Utf8StreamBuffer buffer;
    const std::string nihao = u8"\u4F60\u597D";

    EXPECT_TRUE(buffer.push(std::string(1, nihao[0])).empty());
    EXPECT_TRUE(buffer.push(nihao.substr(1, 1)).empty());
    EXPECT_EQ(buffer.push(nihao.substr(2)), nihao);
    EXPECT_TRUE(buffer.flush().empty());
}

TEST(Utf8StreamBuffer, FlushEmitsPendingTail) {
    qtrans::text::Utf8StreamBuffer buffer;
    const std::string partial = std::string(1, static_cast<char>(0xE4));
    EXPECT_TRUE(buffer.push(partial).empty());
    EXPECT_EQ(buffer.flush(), partial);
}
