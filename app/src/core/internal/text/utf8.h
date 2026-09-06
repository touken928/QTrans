#pragma once

#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace qtrans::core {

bool is_valid_utf8(std::string_view text);
std::string sanitize_utf8(std::string_view text);

size_t complete_prefix_length(std::string_view text);

size_t next_code_point_index(std::string_view text, size_t index);

void validate_or_throw(std::string_view text);

}  // namespace qtrans::core
