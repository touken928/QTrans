#pragma once

#include <cstddef>
#include <stdexcept>
#include <string_view>

namespace qtrans::text {

bool is_valid_utf8(std::string_view text);

// Longest prefix ending on complete UTF-8 code points. Trailing incomplete sequences are excluded.
size_t complete_prefix_length(std::string_view text);

// Index after the code point starting at index, or text.size() if index is at/ past the end.
size_t next_code_point_index(std::string_view text, size_t index);

void validate_or_throw(std::string_view text);

}  // namespace qtrans::text
