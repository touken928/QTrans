#pragma once

#include <functional>
#include <memory>
#include <string>

namespace qtrans::core {

struct PromptFormatter {
    std::function<std::string(const std::string &text, const std::string &target_language)>
        build_user_prompt;
    std::function<std::string(const std::string &user_prompt)> format_chat_prompt;

    bool is_configured() const {
        return static_cast<bool>(build_user_prompt) && static_cast<bool>(format_chat_prompt);
    }

    std::string format_translation_prompt(const std::string &text,
                                          const std::string &target_language) const {
        return format_chat_prompt(build_user_prompt(text, target_language));
    }
};

using PromptFormatterPtr = std::shared_ptr<const PromptFormatter>;

}  // namespace qtrans::core
