#include "utf8_stream_buffer.h"

#include "utf8.h"

#include <simdutf.h>

namespace qtrans::core {

std::string Utf8StreamBuffer::push(std::string_view piece) {
    pending_.append(piece);
    std::string output;
    while (!pending_.empty()) {
        const auto result = simdutf::validate_utf8_with_errors(pending_.data(), pending_.size());
        if (result.error == simdutf::error_code::SUCCESS) {
            output += pending_;
            pending_.clear();
            break;
        }
        if (result.count > 0) {
            output.append(pending_.substr(0, result.count));
            pending_.erase(0, result.count);
        }
        if (result.error == simdutf::error_code::TOO_SHORT) break;
        if (!pending_.empty()) pending_.erase(0, 1);
    }
    return output;
}

std::string Utf8StreamBuffer::flush() {
    std::string emit = push({});
    pending_.clear();
    return emit;
}

}  // namespace qtrans::core
