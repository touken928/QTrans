#pragma once

#include <string>

namespace qtrans::core {

std::string translation_chinese_name(const std::string &model_name);
bool is_chinese_language_name(const std::string &model_name);

}  // namespace qtrans::core
