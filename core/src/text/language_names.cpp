#include "language_names.h"

#include <cstring>

namespace qtrans::core {

namespace {

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

bool match_model_name(const std::string &a, const std::string &b) {
    if (a.empty() || b.empty()) {
        return false;
    }
    return equals_ignore_case(a.c_str(), b.c_str());
}

bool is_chinese_id(const std::string &model_name) {
    // 9 known languages, only Chinese has id "zh"
    static const std::pair<const char *, const char *> kMap[] = {
        {"Chinese", "zh"},
        {"Japanese", "ja"},
        {"Korean", "ko"},
        {"English", "en"},
        {"French", "fr"},
        {"German", "de"},
        {"Spanish", "es"},
        {"Arabic", "ar"},
        {"Russian", "ru"},
    };
    for (auto &entry : kMap) {
        if (match_model_name(model_name, entry.first)) {
            return std::strcmp(entry.second, "zh") == 0;
        }
    }
    return false;
}

}  // namespace

std::string translation_chinese_name(const std::string &model_name) {
    static const std::pair<const char *, const char *> kChineseNames[] = {
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
    for (auto &entry : kChineseNames) {
        if (match_model_name(model_name, entry.first)) {
            return entry.second;
        }
    }
    return model_name;
}

bool is_chinese_language_name(const std::string &model_name) {
    if (is_chinese_id(model_name)) {
        return true;
    }
    return model_name == "中文" ||
           model_name == "繁体中文" ||
           model_name == "Traditional Chinese" ||
           model_name == "zh" ||
           model_name == "zh-Hant";
}

}  // namespace qtrans::core
