#pragma once

#include <functional>
#include <string>
#include <vector>

namespace qtrans::core {

std::vector<std::string> chunk_by_token_budget(
    const std::string &utf8_text,
    int max_tokens,
    const std::function<int(const std::string &)> &token_counter);

}  // namespace qtrans::core
