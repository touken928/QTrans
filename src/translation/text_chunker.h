#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

// Tokens reserved for model output so prompt + generation stays within n_ctx.
constexpr int kTranslationOutputReserve = 512;

// Splits UTF-8 text into ICU sentence segments. Concatenating the segments
// reproduces the original text.
std::vector<std::string> split_sentences_icu(const std::string &utf8_text);

// Greedy-merge segments into chunks where token_counter(chunk) <= max_tokens.
// When a single segment exceeds max_tokens it is split on UTF-8 boundaries.
// Concatenating the returned chunks reproduces utf8_text.
std::vector<std::string> chunk_text_by_token_budget(
    const std::string &utf8_text,
    int max_tokens,
    const std::function<int(const std::string &)> &token_counter);
