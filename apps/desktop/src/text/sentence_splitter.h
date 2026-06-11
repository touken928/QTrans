#pragma once

#include <string>
#include <vector>

namespace qtrans::text {

// Splits UTF-8 text into ICU sentence segments. Concatenating the segments
// reproduces the original text.
std::vector<std::string> split_sentences(const std::string &utf8_text);

}  // namespace qtrans::text
