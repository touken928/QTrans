#pragma once

#include <string>
#include <vector>

namespace qtrans::core {

std::vector<std::string> split_sentences(const std::string &utf8_text);

}  // namespace qtrans::core
