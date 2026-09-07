#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace sentbreak {

class Segmenter {
public:
    static Segmenter uax29();

    [[nodiscard]] std::vector<std::size_t> boundaries(
        std::string_view text) const;
    [[nodiscard]] std::vector<std::string_view> split(
        std::string_view text) const;

private:
    enum class Algorithm { Uax29 };

    explicit Segmenter(Algorithm algorithm);
    Algorithm algorithm_;
};

}  // namespace sentbreak
