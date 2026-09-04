#include "sentence_splitter.h"

#if QTRANS_USE_ICU
#include <unicode/brkiter.h>
#include <unicode/unistr.h>
#endif

#include <memory>

namespace qtrans::core {

namespace {

#if QTRANS_USE_ICU
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
#endif

}  // namespace

std::vector<std::string> split_sentences(const std::string &utf8_text) {
    if (utf8_text.empty()) {
        return {};
    }

#if !QTRANS_USE_ICU
    std::vector<std::string> sentences;
    std::size_t start = 0;
    for (std::size_t index = 0; index < utf8_text.size(); ++index) {
        const unsigned char byte = static_cast<unsigned char>(utf8_text[index]);
        const bool ascii_boundary = byte == '.' || byte == '!' || byte == '?' || byte == '\n';
        const bool cjk_boundary =
            index + 2 < utf8_text.size() &&
            ((byte == 0xE3 && static_cast<unsigned char>(utf8_text[index + 1]) == 0x80 &&
              (static_cast<unsigned char>(utf8_text[index + 2]) == 0x82 ||
               static_cast<unsigned char>(utf8_text[index + 2]) == 0x81 ||
               static_cast<unsigned char>(utf8_text[index + 2]) == 0x89)) ||
             (byte == 0xEF && static_cast<unsigned char>(utf8_text[index + 1]) == 0xBC &&
              (static_cast<unsigned char>(utf8_text[index + 2]) == 0x81 ||
               static_cast<unsigned char>(utf8_text[index + 2]) == 0x9F)));
        if (ascii_boundary || cjk_boundary) {
            const std::size_t end = index + (cjk_boundary ? 3 : 1);
            sentences.push_back(utf8_text.substr(start, end - start));
            start = end;
            index = end - 1;
        }
    }
    if (start < utf8_text.size()) {
        sentences.push_back(utf8_text.substr(start));
    }
    return sentences.empty() ? std::vector<std::string>{utf8_text} : sentences;
#else
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
#endif
}

}  // namespace qtrans::core
