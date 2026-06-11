#include "utf8_stream_buffer.h"

#include "utf8.h"

namespace qtrans::core {

std::string Utf8StreamBuffer::push(std::string_view piece) {
    pending_.append(piece);
    const size_t complete_len = complete_prefix_length(pending_);
    if (complete_len == 0) {
        return {};
    }
    std::string emit = pending_.substr(0, complete_len);
    pending_.erase(0, complete_len);
    return emit;
}

std::string Utf8StreamBuffer::flush() {
    std::string emit = std::move(pending_);
    pending_.clear();
    return emit;
}

}  // namespace qtrans::core
