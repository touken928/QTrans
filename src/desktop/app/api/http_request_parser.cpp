#include "app/api/http_request_parser.h"

namespace qtrans::app::http {

namespace {

ParseResult error(int status, const QString &message) {
    ParseResult result;
    result.state = ParseState::Error;
    result.error_status = status;
    result.error_message = message;
    return result;
}

}  // namespace

ParseResult parse_request(const QByteArray &buffer, RequestLimits limits) {
    const int header_end = buffer.indexOf("\r\n\r\n");
    if (header_end < 0) {
        if (buffer.size() > limits.max_header_bytes) {
            return error(431, QStringLiteral("request header fields too large"));
        }
        return {};
    }
    if (header_end > limits.max_header_bytes) {
        return error(431, QStringLiteral("request header fields too large"));
    }

    ParseResult result;
    Request &request = result.request;
    const QByteArray head = buffer.left(header_end);
    const int line_end = head.indexOf("\r\n");
    if (line_end < 0) return error(400, QStringLiteral("malformed HTTP request"));

    const QList<QByteArray> request_line = head.left(line_end).split(' ');
    if (request_line.size() != 3 || request_line[0].isEmpty() ||
        request_line[1].isEmpty() || request_line[2] != "HTTP/1.1") {
        return error(400, QStringLiteral("malformed HTTP request"));
    }
    request.method = QString::fromLatin1(request_line[0]);
    request.path = QString::fromLatin1(request_line[1]);

    int position = line_end + 2;
    while (position < head.size()) {
        const int next = head.indexOf("\r\n", position);
        const int end = next < 0 ? head.size() : next;
        const QByteArray line = head.mid(position, end - position);
        const int colon = line.indexOf(':');
        if (colon <= 0) return error(400, QStringLiteral("malformed HTTP header"));
        const QByteArray name = line.left(colon).trimmed().toLower();
        const QByteArray value = line.mid(colon + 1).trimmed();
        if (name.isEmpty()) return error(400, QStringLiteral("malformed HTTP header"));
        if (name == "content-length" && request.headers.contains(name)) {
            return error(400, QStringLiteral("duplicate Content-Length header"));
        }
        request.headers.insert(name, value);
        if (next < 0) break;
        position = next + 2;
    }

    if (request.headers.contains("transfer-encoding")) {
        return error(400, QStringLiteral("Transfer-Encoding is not supported"));
    }

    const bool is_post = request.method.compare(QStringLiteral("POST"),
                                                Qt::CaseInsensitive) == 0;
    const auto length = request.headers.constFind("content-length");
    if (is_post && length == request.headers.cend()) {
        return error(411, QStringLiteral("Content-Length is required"));
    }

    qint64 body_size = 0;
    if (length != request.headers.cend()) {
        bool valid = false;
        body_size = length.value().toLongLong(&valid);
        if (!valid || body_size < 0) {
            return error(400, QStringLiteral("invalid Content-Length"));
        }
        if (body_size > limits.max_body_bytes) {
            return error(413, QStringLiteral("request body too large"));
        }
    }

    const QByteArray trailing = buffer.mid(header_end + 4);
    if (is_post && trailing.size() < body_size) return {};
    if (is_post) request.body = trailing.left(static_cast<int>(body_size));
    result.state = ParseState::Complete;
    return result;
}

}  // namespace qtrans::app::http
