#include "sentence_splitter.h"

#include <sentbreak/sentbreak.h>

namespace qtrans::core {

std::vector<std::string> split_sentences(const std::string &utf8_text) {
    if (utf8_text.empty()) {
        return {};
    }

    const auto views = sentbreak::Segmenter::uax29().split(utf8_text);
    std::vector<std::string> sentences;
    sentences.reserve(views.size());
    for (const std::string_view view : views) {
        sentences.emplace_back(view);
    }
    return sentences;
}

}  // namespace qtrans::core
