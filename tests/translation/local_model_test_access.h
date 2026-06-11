#pragma once

#include "translation/local_model.h"
#include "translation/model_profile.h"

#include <string>

struct LocalModelTestAccess {
    static std::string build_user_prompt(const std::string &text, const std::string &target_language) {
        return LocalModel::build_user_prompt(text, target_language);
    }

    static std::string format_chat_prompt(const std::string &user_prompt) {
        LocalModel hymt(hymt18b_q4_profile());
        return hymt.format_chat_prompt(user_prompt);
    }

    static bool contains_chinese(const std::string &text) {
        return LocalModel::contains_chinese(text);
    }
};
