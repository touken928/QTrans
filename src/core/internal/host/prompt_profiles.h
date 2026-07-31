#pragma once

#include "qtrans/core.h"

#include <string>

namespace qtrans::core::host_detail {

enum class PromptProfileId { Hymt2SevenB,
                             Hymt2EighteenB };

struct PromptProfile {
    PromptProfileId id;
    int context_tokens;
    int default_output_tokens;
    bool supports_conversation;

    [[nodiscard]] Failure render(const InvocationInput &input, std::string &prompt) const;
};

[[nodiscard]] Failure select_prompt_profile(const ModelId &model, PromptProfile &profile);

}  // namespace qtrans::core::host_detail
