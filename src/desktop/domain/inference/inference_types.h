#pragma once

#include <cstdint>
#include <string>

// Typed desktop inference domain contracts. These types are Qt-free and live
// outside core; InferenceService adapts them to and from the core ModelHost
// invocation domain.

struct TranslationJobId {
    std::uint64_t value = 0;

    bool operator==(const TranslationJobId &other) const {
        return value == other.value;
    }
    bool operator!=(const TranslationJobId &other) const {
        return value != other.value;
    }
    bool is_valid() const {
        return value != 0;
    }
};

enum class TranslationState {
    Pending,
    Running,
    Completed,
    Failed,
    Cancelled,
    Preempted,
};

enum class TranslationChannel {
    Target,
    BackTranslate,
};

struct NativeTranslationRequest {
    std::string source;
    std::string target_language;
    std::string source_language;
    bool back_translate = false;
    bool wordselect = false;
};

struct BatchTranslationRequest {
    std::string source;
    std::string target_language;
    std::string source_language;
};

struct TranslationJobResult {
    TranslationJobId id;
    TranslationState state = TranslationState::Pending;
    std::string error_message;
};
