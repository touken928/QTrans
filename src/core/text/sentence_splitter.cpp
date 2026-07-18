#include "sentence_splitter.h"

#include <unicode/brkiter.h>
#include <unicode/unistr.h>

#include <memory>

namespace qtrans::core {

namespace {

std::string extract_utf8_slice(const icu::UnicodeString &text, int32_t start, int32_t length) {
    if (length <= 0) {
        return {};
    }
    icu::UnicodeString slice;
    text.extract(start, length, slice);
    std::string out;
    slice.toUTF8String(out);
    return out;
}

}  // namespace

std::vector<std::string> split_sentences(const std::string &utf8_text) {
    if (utf8_text.empty()) {
        return {};
    }

    UErrorCode status = U_ZERO_ERROR;
    icu::UnicodeString unicode = icu::UnicodeString::fromUTF8(utf8_text);
    std::unique_ptr<icu::BreakIterator> iterator(
        icu::BreakIterator::createSentenceInstance(icu::Locale::getRoot(), status));
    if (U_FAILURE(status) || iterator == nullptr) {
        return {utf8_text};
    }

    iterator->setText(unicode);

    std::vector<std::string> sentences;
    int32_t start = iterator->first();
    for (int32_t end = iterator->next(); end != icu::BreakIterator::DONE;
         start = end, end = iterator->next()) {
        sentences.push_back(extract_utf8_slice(unicode, start, end - start));
    }

    if (sentences.empty()) {
        return {utf8_text};
    }
    return sentences;
}

}  // namespace qtrans::core
