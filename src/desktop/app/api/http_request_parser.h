#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>

namespace qtrans::app::http {

struct RequestLimits {
    int max_header_bytes = 0;
    int max_body_bytes = 0;
};

struct Request {
    QString method;
    QString path;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
};

enum class ParseState { Incomplete,
                        Complete,
                        Error };

struct ParseResult {
    ParseState state = ParseState::Incomplete;
    Request request;
    int error_status = 0;
    QString error_message;
};

// Parses exactly one bounded HTTP/1.1 request. The local API closes every
// connection after one response, so pipelining and transfer encoding are
// intentionally unsupported.
ParseResult parse_request(const QByteArray &buffer, RequestLimits limits);

}  // namespace qtrans::app::http
