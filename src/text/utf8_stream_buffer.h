#pragma once

#include <string>
#include <string_view>

namespace qtrans::text {

// BPE tokens may split UTF-8 code points; only emit complete characters to consumers.
class Utf8StreamBuffer {
public:
    std::string push(std::string_view piece);
    std::string flush();

private:
    std::string pending_;
};

}  // namespace qtrans::text
