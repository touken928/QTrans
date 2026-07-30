#include "domain/model-adapters/hymt2_7b_local_model.h"

#include <cstring>
#include <utility>

namespace {

constexpr const char k_bos[] = "<|startoftext|>";
constexpr const char k_user_end[] = "<|extra_0|>";

bool equals_ignore_case(const char *a, const char *b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        const char ca = *a >= 'A' && *a <= 'Z' ? static_cast<char>(*a - 'A' + 'a') : *a;
        const char cb = *b >= 'A' && *b <= 'Z' ? static_cast<char>(*b - 'A' + 'a') : *b;
        if (ca != cb) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

bool match_language_name(const std::string &a, const std::string &b) {
    if (a.empty() || b.empty()) {
        return false;
    }
    return equals_ignore_case(a.c_str(), b.c_str());
}

std::string chinese_target_name(const std::string &target_language) {
    static const std::pair<const char *, const char *> k_names[] = {
        {"Chinese", "中文"},
        {"Japanese", "日语"},
        {"Korean", "韩语"},
        {"English", "英语"},
        {"French", "法语"},
        {"German", "德语"},
        {"Spanish", "西班牙语"},
        {"Arabic", "阿拉伯语"},
        {"Russian", "俄语"},
    };
    for (const auto &entry : k_names) {
        if (match_language_name(target_language, entry.first)) {
            return entry.second;
        }
    }
    return target_language;
}

bool is_chinese_target(const std::string &target_language) {
    if (match_language_name(target_language, "Chinese")) {
        return true;
    }
    return target_language == "中文" ||
           target_language == "繁体中文" ||
           target_language == "Traditional Chinese" ||
           target_language == "zh" ||
           target_language == "zh-Hant";
}

bool contains_chinese(const std::string &text) {
    for (size_t i = 0; i < text.size();) {
        const unsigned char b = static_cast<unsigned char>(text[i]);
        if (b >= 0xE0 && b <= 0xEF && i + 2 < text.size()) {
            const uint32_t cp = ((b & 0x0F) << 12) |
                                ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                                (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            if (cp >= 0x4E00 && cp <= 0x9FFF) {
                return true;
            }
            i += 3;
        } else {
            ++i;
        }
    }
    return false;
}

std::string build_user_prompt(const std::string &text, const std::string &target_language) {
    if (target_language == "Auto") {
        return "Translate the following segment:\n\n" + text;
    }

    if (contains_chinese(text) && !is_chinese_target(target_language)) {
        return "将以下文本翻译为" + chinese_target_name(target_language) +
               "，注意只需要输出翻译后的结果，不要额外解释：\n\n" + text;
    }

    return "Translate the following segment into " + target_language +
           ", without additional explanation.\n\n" + text;
}

std::string format_chat_prompt(const std::string &user_prompt) {
    return std::string(k_bos) + user_prompt + k_user_end;
}

qtrans::core::GenerationOptions make_generation_options() {
    qtrans::core::GenerationOptions opts;
    opts.context_tokens = 8192;
    opts.max_output_tokens = 8192;
    return opts;
}

}  // namespace

qtrans::core::Model make_hymt2_7b_local_model(
    const std::filesystem::path &path) {
    qtrans::core::Model model;
    model.path = path;
    model.prompt_formatter = [](std::string_view text, std::string_view target_language) {
        return format_chat_prompt(build_user_prompt(std::string(text), std::string(target_language)));
    };
    model.generation = make_generation_options();
    return model;
}
