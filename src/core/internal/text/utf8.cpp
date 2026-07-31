#include "utf8.h"

#include <simdutf.h>

#include <string>

namespace qtrans::core {

bool is_valid_utf8(std::string_view text) {
    if (text.empty()) {
        return true;
    }
    return simdutf::validate_utf8(text.data(), text.size());
}

std::string sanitize_utf8(std::string_view text) {
    std::string output;
    size_t offset = 0;
    while (offset < text.size()) {
        const auto result = simdutf::validate_utf8_with_errors(text.data() + offset, text.size() - offset);
        if (result.error == simdutf::error_code::SUCCESS) {
            output.append(text.substr(offset));
            break;
        }
        if (result.count > 0) output.append(text.substr(offset, result.count));
        if (result.error == simdutf::error_code::TOO_SHORT) break;
        offset += result.count + 1;
    }
    return output;
}

size_t complete_prefix_length(std::string_view text) {
    if (text.empty()) {
        return 0;
    }

    size_t index = 0;
    while (index < text.size()) {
        const auto result =
            simdutf::validate_utf8_with_errors(text.data() + index, text.size() - index);
        if (result.error == simdutf::error_code::SUCCESS) {
            return text.size();
        }
        if (result.error == simdutf::error_code::TOO_SHORT) {
            return index + result.count;
        }
        return index + result.count;
    }
    return index;
}

size_t next_code_point_index(std::string_view text, size_t index) {
    if (index >= text.size()) {
        return text.size();
    }

    const auto head = simdutf::validate_utf8_with_errors(text.data() + index, text.size() - index);
    if (head.error == simdutf::error_code::SUCCESS) {
        for (size_t len = 1; len <= text.size() - index; ++len) {
            const auto result = simdutf::validate_utf8_with_errors(text.data() + index, len);
            if (result.error == simdutf::error_code::SUCCESS) {
                return index + len;
            }
        }
        return text.size();
    }
    if (head.error == simdutf::error_code::TOO_SHORT) {
        if (head.count > 0) {
            return index + head.count;
        }
        return text.size();
    }
    return index + 1;
}

void validate_or_throw(std::string_view text) {
    if (text.empty()) {
        return;
    }
    const auto result = simdutf::validate_utf8_with_errors(text.data(), text.size());
    if (result.error != simdutf::error_code::SUCCESS) {
        throw std::invalid_argument("invalid UTF-8 text");
    }
}

}  // namespace qtrans::core
