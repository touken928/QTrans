#include <gtest/gtest.h>

#include "qtrans/core.h"

TEST(CoreModel, GenerationDefaultsAreFacadeOwned) {
    const qtrans::core::Model model;
    EXPECT_EQ(model.generation.context_tokens, 4096);
    EXPECT_EQ(model.generation.max_output_tokens, 4096);
    EXPECT_FLOAT_EQ(model.generation.temperature, 0.7f);
}

TEST(CoreTranslation, OverflowPoliciesAreSingleDirection) {
    qtrans::core::TranslationRequest split{"text", "Chinese", qtrans::core::OverflowPolicy::Split};
    qtrans::core::TranslationRequest reject{"text", "Chinese", qtrans::core::OverflowPolicy::Reject};
    EXPECT_EQ(split.overflow, qtrans::core::OverflowPolicy::Split);
    EXPECT_EQ(reject.overflow, qtrans::core::OverflowPolicy::Reject);
}

TEST(CoreTranslation, ErrorCodesAreStructured) {
    qtrans::core::TranslationResult result;
    result.error_code = qtrans::core::TranslationErrorCode::ContextLimit;
    result.message = "context details";
    EXPECT_EQ(result.error_code, qtrans::core::TranslationErrorCode::ContextLimit);
}
