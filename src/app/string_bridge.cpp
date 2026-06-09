#include "app/string_bridge.h"

#include "text/utf8.h"

#include <QByteArray>

namespace qtrans::app {

std::string to_utf8(const QString &text) {
    const QByteArray bytes = text.toUtf8();
    const std::string out(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    qtrans::text::validate_or_throw(out);
    return out;
}

QString from_utf8(std::string_view utf8) {
    // Engine output may include a truncated UTF-8 tail from BPE streaming; Qt replaces
    // invalid sequences instead of throwing (unlike strict validate_or_throw).
    return QString::fromUtf8(utf8.data(), static_cast<int>(utf8.size()));
}

}  // namespace qtrans::app
