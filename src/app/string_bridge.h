#pragma once

#include <QString>
#include <string>
#include <string_view>

namespace qtrans::app {

// UI → engine: validates UTF-8 before crossing the boundary.
std::string to_utf8(const QString &text);
// Engine → UI: tolerant conversion (invalid bytes become U+FFFD).
QString from_utf8(std::string_view utf8);

}  // namespace qtrans::app
