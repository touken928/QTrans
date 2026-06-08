#include "translation/text_chunker.h"

#include <unicode/brkiter.h>
#include <unicode/unistr.h>

#include <algorithm>
#include <memory>
#include <stdexcept>

namespace {

bool is_utf8_continuation(unsigned char byte) {
    return (byte & 0xC0) == 0x80;
}

size_t next_utf8_code_point_boundary(const std::string &text, size_t index) {
    if (index >= text.size()) {
        return text.size();
    }
    size_t pos = index + 1;
    while (pos < text.size() && is_utf8_continuation(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    return pos;
}

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

std::string max_fitting_prefix(
    const std::string &text,
    int max_tokens,
    const std::function<int(const std::string &)> &token_counter) {
    if (text.empty() || max_tokens <= 0) {
        return {};
    }
    if (token_counter(text) <= max_tokens) {
        return text;
    }

    size_t pos = 0;
    size_t last_good = 0;
    while (pos < text.size()) {
        const size_t next = next_utf8_code_point_boundary(text, pos);
        const std::string prefix = text.substr(0, next);
        if (token_counter(prefix) > max_tokens) {
            break;
        }
        last_good = next;
        pos = next;
    }

    if (last_good == 0) {
        return {};
    }
    return text.substr(0, last_good);
}

void append_hard_split_chunks(
    const std::string &segment,
    int max_tokens,
    const std::function<int(const std::string &)> &token_counter,
    std::vector<std::string> &chunks) {
    std::string remaining = segment;
    while (!remaining.empty()) {
        const std::string piece = max_fitting_prefix(remaining, max_tokens, token_counter);
        if (piece.empty()) {
            throw std::runtime_error("unable to split text to fit model context");
        }
        chunks.push_back(piece);
        remaining.erase(0, piece.size());
    }
}

}  // namespace

std::vector<std::string> split_sentences_icu(const std::string &utf8_text) {
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

std::vector<std::string> chunk_text_by_token_budget(
    const std::string &utf8_text,
    int max_tokens,
    const std::function<int(const std::string &)> &token_counter) {
    if (utf8_text.empty()) {
        return {};
    }
    if (max_tokens <= 0) {
        throw std::invalid_argument("max_tokens must be positive");
    }

    if (token_counter(utf8_text) <= max_tokens) {
        return {utf8_text};
    }

    const std::vector<std::string> sentences = split_sentences_icu(utf8_text);
    std::vector<std::string> chunks;
    std::string current;

    auto flush_current = [&]() {
        if (!current.empty()) {
            chunks.push_back(current);
            current.clear();
        }
    };

    for (const std::string &sentence : sentences) {
        if (sentence.empty()) {
            continue;
        }

        if (token_counter(sentence) > max_tokens) {
            flush_current();
            append_hard_split_chunks(sentence, max_tokens, token_counter, chunks);
            continue;
        }

        const std::string candidate = current + sentence;
        if (current.empty() || token_counter(candidate) <= max_tokens) {
            current = candidate;
        } else {
            flush_current();
            current = sentence;
        }
    }

    flush_current();

    if (chunks.empty()) {
        return {utf8_text};
    }

    std::string rebuilt;
    rebuilt.reserve(utf8_text.size());
    for (const std::string &chunk : chunks) {
        rebuilt += chunk;
    }
    if (rebuilt != utf8_text) {
        throw std::runtime_error("chunking produced non-destructive violation");
    }

    return chunks;
}
