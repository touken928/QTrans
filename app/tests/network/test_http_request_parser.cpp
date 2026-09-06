#include "app/api/http_request_parser.h"

#include <gtest/gtest.h>

using qtrans::app::http::ParseState;

TEST(HttpRequestParser, WaitsForDeclaredBody) {
    const auto parsed = qtrans::app::http::parse_request(
        "POST /v1/chat/completions HTTP/1.1\r\nContent-Length: 4\r\n\r\n{}",
        {1024, 1024});
    EXPECT_EQ(parsed.state, ParseState::Incomplete);
}

TEST(HttpRequestParser, ParsesCompleteRequestAndQuery) {
    const auto parsed = qtrans::app::http::parse_request(
        "POST /v1/chat/completions?x=1 HTTP/1.1\r\n"
        "Content-Type: application/json\r\nContent-Length: 2\r\n\r\n{}",
        {1024, 1024});
    ASSERT_EQ(parsed.state, ParseState::Complete);
    EXPECT_EQ(parsed.request.path, QStringLiteral("/v1/chat/completions?x=1"));
    EXPECT_EQ(parsed.request.body, "{}");
}

TEST(HttpRequestParser, RejectsAmbiguousFraming) {
    const auto duplicate = qtrans::app::http::parse_request(
        "POST / HTTP/1.1\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n",
        {1024, 1024});
    EXPECT_EQ(duplicate.state, ParseState::Error);
    EXPECT_EQ(duplicate.error_status, 400);

    const auto chunked = qtrans::app::http::parse_request(
        "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n",
        {1024, 1024});
    EXPECT_EQ(chunked.state, ParseState::Error);
    EXPECT_EQ(chunked.error_status, 400);
}

TEST(HttpRequestParser, EnforcesHeaderAndBodyCaps) {
    const auto header = qtrans::app::http::parse_request(
        "GET / HTTP/1.1\r\nLong: value", {8, 1024});
    EXPECT_EQ(header.error_status, 431);

    const auto body = qtrans::app::http::parse_request(
        "POST / HTTP/1.1\r\nContent-Length: 9\r\n\r\n", {1024, 8});
    EXPECT_EQ(body.error_status, 413);
}
