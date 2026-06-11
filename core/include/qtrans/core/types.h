#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace qtrans::core {

enum class TranslationOutcome {
    Completed,
    Cancelled,
    Failed,
};

struct TranslationResult {
    TranslationOutcome outcome = TranslationOutcome::Failed;
    std::string text;
    std::string error_message;
};

struct TranslationRequest {
    std::string source;
    std::string target_language;
    std::string source_language;
    bool back_translate = false;
    bool wordselect = false;
};

}  // namespace qtrans::core
