#pragma once

#include <string>

struct TranslationLanguage {
    const char * id;
    const char * model_name;
    const char * chinese_name;
    const char * label;
};

const TranslationLanguage * translation_languages();
int translation_language_count();

const TranslationLanguage * find_translation_language_by_model_name(const std::string & model_name);

std::string translation_chinese_name(const std::string & model_name);

bool is_chinese_language_name(const std::string & model_name);
