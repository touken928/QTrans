#include "core/translation_languages.h"

#include <cstring>

namespace {

constexpr TranslationLanguage kLanguages[] = {
    {"zh", "Chinese", "Chinese"},
    {"ja", "Japanese", "Japanese"},
    {"ko", "Korean", "Korean"},
    {"en", "English", "English"},
    {"fr", "French", "French"},
    {"de", "German", "German"},
    {"es", "Spanish", "Spanish"},
    {"ar", "Arabic", "Arabic"},
    {"ru", "Russian", "Russian"},
};

bool equals_ignore_case(const char * a, const char * b) {
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

} // namespace

const TranslationLanguage * translation_languages() {
    return kLanguages;
}

int translation_language_count() {
    return static_cast<int>(sizeof(kLanguages) / sizeof(kLanguages[0]));
}

const TranslationLanguage * find_translation_language_by_model_name(const std::string & model_name) {
    for (const TranslationLanguage & language : kLanguages) {
        if (equals_ignore_case(language.model_name, model_name.c_str())) {
            return &language;
        }
    }
    return nullptr;
}

bool is_chinese_language_name(const std::string & model_name) {
    const TranslationLanguage * language = find_translation_language_by_model_name(model_name);
    if (language != nullptr && std::strcmp(language->id, "zh") == 0) {
        return true;
    }

    return model_name == "中文" ||
           model_name == "繁体中文" ||
           model_name == "Traditional Chinese" ||
           model_name == "zh" ||
           model_name == "zh-Hant";
}
