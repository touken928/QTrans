#include "app/api/openai_protocol.h"

#include <gtest/gtest.h>

#include <QJsonDocument>
#include <QJsonObject>

TEST(OpenAiProtocol, MapsFailureClassesToHttpStatus) {
    EXPECT_EQ(qtrans::app::openai::status_for_failure(
                  {qtrans::core::FailureCode::NotLoaded, {}}),
              503);
    EXPECT_EQ(qtrans::app::openai::status_for_failure(
                  {qtrans::core::FailureCode::Deadline, {}}),
              504);
    EXPECT_EQ(qtrans::app::openai::status_for_failure(
                  {qtrans::core::FailureCode::Backpressure, {}}),
              429);
    EXPECT_EQ(qtrans::app::openai::status_for_failure(
                  {qtrans::core::FailureCode::InvalidRequest, {}}),
              400);
}

TEST(OpenAiProtocol, BuildsOpenAiCompatibleErrorShape) {
    const auto document = QJsonDocument::fromJson(
        qtrans::app::openai::error_json("bad request", "invalid_request_error"));
    ASSERT_TRUE(document.isObject());
    const auto error = document.object().value("error").toObject();
    EXPECT_EQ(error.value("message").toString(), "bad request");
    EXPECT_EQ(error.value("type").toString(), "invalid_request_error");
    EXPECT_TRUE(error.value("param").isNull());
}
