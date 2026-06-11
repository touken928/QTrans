#pragma once

#include <string>
#include <string_view>

namespace qtrans::core {

class Utf8StreamBuffer {
public:
    std::string push(std::string_view piece);
    std::string flush();

private:
    std::string pending_;
};

}  // namespace qtrans::core
