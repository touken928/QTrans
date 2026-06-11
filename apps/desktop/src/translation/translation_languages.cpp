#include "translation/translation_languages.h"

#include <cstring>

namespace {

constexpr TranslationLanguage kLanguages[] = {
    {"zh", "Chinese", "中文", "Chinese"},
    {"ja", "Japanese", "日语", "Japanese"},
    {"ko", "Korean", "韩语", "Korean"},
    {"en", "English", "英语", "English"},
    {"fr", "French", "法语", "French"},
    {"de", "German", "德语", "German"},
    {"es", "Spanish", "西班牙语", "Spanish"},
    {"ar", "Arabic", "阿拉伯语", "Arabic"},
    {"ru", "Russian", "俄语", "Russian"},
};

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

}  // namespace

const TranslationLanguage *translation_languages() {
    return kLanguages;
}

int translation_language_count() {
    return static_cast<int>(sizeof(kLanguages) / sizeof(kLanguages[0]));
}

const TranslationLanguage *find_translation_language_by_model_name(const std::string &model_name) {
    for (const TranslationLanguage &language : kLanguages) {
        if (equals_ignore_case(language.model_name, model_name.c_str())) {
            return &language;
        }
    }
    return nullptr;
}

std::string translation_chinese_name(const std::string &model_name) {
    const TranslationLanguage *language = find_translation_language_by_model_name(model_name);
    if (language != nullptr) {
        return language->chinese_name;
    }
    return model_name;
}

bool is_chinese_language_name(const std::string &model_name) {
    const TranslationLanguage *language = find_translation_language_by_model_name(model_name);
    if (language != nullptr && std::strcmp(language->id, "zh") == 0) {
        return true;
    }

    return model_name == "中文" ||
           model_name == "繁体中文" ||
           model_name == "Traditional Chinese" ||
           model_name == "zh" ||
           model_name == "zh-Hant";
}
