#pragma once

#include "translation/hymt.h"

#include <string>

struct HymtTestAccess {
    static std::string build_user_prompt(const std::string &text, const std::string &target_language) {
        return Hymt::build_user_prompt(text, target_language);
    }

    static std::string format_chat_prompt(const std::string &user_prompt) {
        return Hymt::format_chat_prompt(user_prompt);
    }

    static bool contains_chinese(const std::string &text) {
        return Hymt::contains_chinese(text);
    }
};
