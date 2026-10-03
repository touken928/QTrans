#include "chunker.h"

#include "sentence_splitter.h"
#include "utf8.h"

#include <stdexcept>
#include <string_view>

namespace qtrans::core {

namespace {

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
        const size_t next = next_code_point_index(text, pos);
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

std::vector<std::string> chunk_by_token_budget(
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

    const std::vector<std::string> sentences = split_sentences(utf8_text);
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

}  // namespace qtrans::core
