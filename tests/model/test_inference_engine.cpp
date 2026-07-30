#include <gtest/gtest.h>

#include "domain/inference/engine/inference_engine.h"

#include <vector>
#include <utility>

namespace {

qtrans::core::TranslationResult completed_result(const std::string &text) {
    return {qtrans::core::TranslationOutcome::Completed,
            qtrans::core::TranslationErrorCode::None,
            text,
            {}};
}

}  // namespace

struct InferenceEngineTestAccess {
    static InferenceEngine make(InferenceEngine::TranslationDriver driver) {
        return InferenceEngine(std::move(driver));
    }
};

TEST(InferenceEngine, PreCancelledRequestDoesNotResetOrCallDriver) {
    std::vector<bool> resets;
    int calls = 0;
    InferenceEngine engine = InferenceEngineTestAccess::make([&](const qtrans::core::TranslationRequest &,
                                                                 qtrans::core::TokenSink,
                                                                 qtrans::core::StopPredicate) {
        ++calls;
        return completed_result("forward");
    });
    CancelToken cancel;
    cancel.cancel();

    const TranslatePipelinePayload payload{"source", "Chinese", "English", true, false};
    const auto result = engine.run_translate_pipeline(
        payload, [&](bool back) { resets.push_back(back); }, {}, &cancel);
    EXPECT_EQ(result.outcome, InferenceOutcome::Cancelled);
    EXPECT_TRUE(resets.empty());
    EXPECT_EQ(calls, 0);
}

TEST(InferenceEngine, ForwardResetOccursBeforeForwardRequest) {
    std::vector<bool> resets;
    std::vector<qtrans::core::OverflowPolicy> policies;
    InferenceEngine engine = InferenceEngineTestAccess::make([&](const qtrans::core::TranslationRequest &request,
                                                                 qtrans::core::TokenSink,
                                                                 qtrans::core::StopPredicate) {
        policies.push_back(request.overflow);
        return completed_result("forward");
    });
    const TranslatePipelinePayload payload{"source", "Chinese", "English", false, false};
    const auto result = engine.run_translate_pipeline(
        payload, [&](bool back) { resets.push_back(back); }, {}, nullptr);
    ASSERT_EQ(result.outcome, InferenceOutcome::Completed);
    ASSERT_EQ(resets, std::vector<bool>{false});
    ASSERT_EQ(policies, std::vector<qtrans::core::OverflowPolicy>{qtrans::core::OverflowPolicy::Split});
}

TEST(InferenceEngine, CancellationBeforeBackTranslationDoesNotResetBackChannel) {
    std::vector<bool> resets;
    int calls = 0;
    CancelToken cancel;
    InferenceEngine engine = InferenceEngineTestAccess::make([&](const qtrans::core::TranslationRequest &,
                                                                 qtrans::core::TokenSink,
                                                                 qtrans::core::StopPredicate) {
        ++calls;
        cancel.cancel();
        return completed_result("forward");
    });
    const TranslatePipelinePayload payload{"source", "Chinese", "English", true, false};
    const auto result = engine.run_translate_pipeline(
        payload, [&](bool back) { resets.push_back(back); }, {}, &cancel);
    EXPECT_EQ(result.outcome, InferenceOutcome::Cancelled);
    EXPECT_EQ(resets, std::vector<bool>{false});
    EXPECT_EQ(calls, 1);
}

TEST(InferenceEngine, WordSelectionRejectsOnlyForwardRequest) {
    std::vector<qtrans::core::OverflowPolicy> policies;
    InferenceEngine engine = InferenceEngineTestAccess::make([&](const qtrans::core::TranslationRequest &request,
                                                                 qtrans::core::TokenSink,
                                                                 qtrans::core::StopPredicate) {
        policies.push_back(request.overflow);
        return completed_result("forward");
    });
    const TranslatePipelinePayload payload{"source", "Chinese", "English", true, true};
    const auto result = engine.run_translate_pipeline(payload, {}, {}, nullptr);
    ASSERT_EQ(result.outcome, InferenceOutcome::Completed);
    ASSERT_EQ(policies.size(), 2U);
    EXPECT_EQ(policies[0], qtrans::core::OverflowPolicy::Reject);
    EXPECT_EQ(policies[1], qtrans::core::OverflowPolicy::Split);
}
