#include "sentbreak/sentbreak.h"

#include <cassert>
#include <string_view>
#include <vector>

int main() {
    const auto segmenter = sentbreak::Segmenter::uax29();
    const std::string_view text = "One. Two!\nThree?";
    assert((segmenter.boundaries(text) == std::vector<std::size_t>{0, 5, 10, 16}));
    assert((segmenter.split(text) ==
            std::vector<std::string_view>{"One. ", "Two!\n", "Three?"}));
    assert((segmenter.split("Mr. Smith") ==
            std::vector<std::string_view>{"Mr. Smith"}));
    assert((segmenter.split("你好。世界") ==
            std::vector<std::string_view>{"你好。", "世界"}));
    assert((segmenter.split("First\r\nSecond") ==
            std::vector<std::string_view>{"First\r\n", "Second"}));
    assert(segmenter.boundaries("") == std::vector<std::size_t>{0});
}
