#include "app/local_api_service.h"

#include "app/inference_service.h"
#include "domain/logging/component.h"
#include "domain/logging/logger.h"
#include "shared/string_bridge.h"

#include <QDateTime>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

const QList<QString> &supported_request_fields() {
    static const QList<QString> fields = {
        QStringLiteral("model"),
        QStringLiteral("messages"),
        QStringLiteral("temperature"),
        QStringLiteral("top_p"),
        QStringLiteral("seed"),
        QStringLiteral("max_tokens"),
        QStringLiteral("max_completion_tokens"),
        QStringLiteral("stream"),
        QStringLiteral("n"),
        QStringLiteral("tools"),
        QStringLiteral("tool_choice"),
    };
    return fields;
}

QByteArray statusPhrase(int status) {
    switch (status) {
        case 200:
            return "OK";
        case 400:
            return "Bad Request";
        case 404:
            return "Not Found";
        case 408:
            return "Request Timeout";
        case 411:
            return "Length Required";
        case 413:
            return "Payload Too Large";
        case 429:
            return "Too Many Requests";
        case 431:
            return "Request Header Fields Too Large";
        case 500:
            return "Internal Server Error";
        case 503:
            return "Service Unavailable";
        case 504:
            return "Gateway Timeout";
        default:
            return "Error";
    }
}

enum class HeadParseResult {
    Ok,
    Malformed,
    DuplicateContentLength,
};

HeadParseResult parseRequestHead(const QByteArray &head, QString *method, QString *path,
                                 QHash<QByteArray, QByteArray> *headers) {
    const int line_end = head.indexOf("\r\n");
    if (line_end < 0) return HeadParseResult::Malformed;
    const QList<QByteArray> parts = head.left(line_end).split(' ');
    if (parts.size() < 3) return HeadParseResult::Malformed;
    *method = QString::fromLatin1(parts.at(0));
    *path = QString::fromLatin1(parts.at(1));

    int pos = line_end + 2;
    while (pos < head.size()) {
        const int next = head.indexOf("\r\n", pos);
        const int end = (next < 0) ? head.size() : next;
        const QByteArray line = head.mid(pos, end - pos);
        if (!line.isEmpty()) {
            const int colon = line.indexOf(':');
            if (colon > 0) {
                const QByteArray name = line.left(colon).trimmed().toLower();
                const QByteArray value = line.mid(colon + 1).trimmed();
                if (name == "content-length" && headers->contains(name)) {
                    return HeadParseResult::DuplicateContentLength;
                }
                headers->insert(name, value);
            }
        }
        if (next < 0) break;
        pos = next + 2;
    }
    return HeadParseResult::Ok;
}

}  // namespace

LocalApiService::LocalApiService(InferenceService *inference_service, QObject *parent)
    : QObject(parent), inference_service_(inference_service) {
}

LocalApiService::~LocalApiService() {
    stop();
}

bool LocalApiService::start(quint16 port, QString *error_message) {
    if (server_ != nullptr && server_->isListening() && server_->serverPort() == port) {
        return true;
    }
    stop();

    auto *server = new QTcpServer(this);
    connect(server, &QTcpServer::newConnection, this, &LocalApiService::handleNewConnection);
    // Loopback IPv4 only: QHostAddress::LocalHost is 127.0.0.1.
    if (!server->listen(QHostAddress::LocalHost, port)) {
        if (error_message != nullptr) *error_message = server->errorString();
        qtrans::log::get(qtrans::log::Component::App)
            ->error("local api failed to listen on port {}: {}", static_cast<unsigned>(port),
                    qtrans::app::to_utf8(server->errorString()));
        server->deleteLater();
        return false;
    }
    server_ = server;
    qtrans::log::get(qtrans::log::Component::App)
        ->info("local api listening on 127.0.0.1:{}", static_cast<unsigned>(server_->serverPort()));
    return true;
}

void LocalApiService::stop() {
    // Snapshot first: abort() synchronously emits disconnected() which runs
    // discardSocket() and mutates the tracked sets while we iterate.
    const QList<QTcpSocket *> sockets = clients_.values();
    for (QTcpSocket *socket : sockets) {
        const auto pending = pending_requests_.find(socket);
        if (pending != pending_requests_.end() && pending.value() != 0) {
            inference_service_->cancelApiChat(pending.value());
        }
        socket->abort();
        socket->deleteLater();
    }
    clients_.clear();
    read_buffers_.clear();
    pending_requests_.clear();
    for (QTimer *timer : request_timers_) timer->deleteLater();
    request_timers_.clear();
    outstanding_requests_ = 0;
    if (server_ != nullptr) {
        server_->close();
        server_->deleteLater();
        server_ = nullptr;
    }
}

bool LocalApiService::isListening() const {
    return server_ != nullptr && server_->isListening();
}

quint16 LocalApiService::port() const {
    return server_ != nullptr ? server_->serverPort() : 0;
}

void LocalApiService::handleNewConnection() {
    while (QTcpSocket *socket = server_->nextPendingConnection()) {
        clients_.insert(socket);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handleReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            handleDisconnected(socket);
        });
        connect(socket, &QTcpSocket::errorOccurred, this, [socket] {
            if (socket->error() != QAbstractSocket::RemoteHostClosedError) {
                qtrans::log::get(qtrans::log::Component::App)
                    ->warn("local api client socket error: {}",
                           qtrans::app::to_utf8(socket->errorString()));
            }
        });

        // Bounded client count: reject new clients once the cap is reached.
        // replyError() closes the connection; the disconnected handler cleans
        // up the rejected socket like any other.
        if (clients_.size() > kMaxClients) {
            replyError(socket, 429, QStringLiteral("too many concurrent connections"),
                       QStringLiteral("rate_limit_error"));
            continue;
        }

        // Per-connection deadline for finishing the request; restarted on
        // every readyRead and cancelled when the request is dispatched.
        auto *timer = new QTimer(socket);
        timer->setSingleShot(true);
        timer->setInterval(kRequestTimeoutMs);
        connect(timer, &QTimer::timeout, this, [this, socket] { handleRequestTimeout(socket); });
        timer->start();
        request_timers_.insert(socket, timer);
    }
}

void LocalApiService::handleReadyRead(QTcpSocket *socket) {
    // One request per connection; ignore any bytes arriving after dispatch.
    if (pending_requests_.contains(socket)) return;

    if (QTimer *timer = request_timers_.value(socket, nullptr)) {
        timer->start();  // reset the unfinished-request deadline
    }

    QByteArray &buffer = read_buffers_[socket];
    buffer.append(socket->readAll());

    if (buffer.size() > kMaxTotalBytes) {
        replyError(socket, 413, QStringLiteral("request body too large"));
        return;
    }

    const int header_end = buffer.indexOf("\r\n\r\n");
    if (header_end < 0) {
        if (buffer.size() > kMaxHeaderBytes) {
            replyError(socket, 431, QStringLiteral("request header fields too large"));
        }
        return;
    }

    const QByteArray head = buffer.left(header_end);
    if (head.size() > kMaxHeaderBytes) {
        replyError(socket, 431, QStringLiteral("request header fields too large"));
        return;
    }
    const QByteArray trailing = buffer.mid(header_end + 4);

    QString method;
    QString path;
    QHash<QByteArray, QByteArray> headers;
    const HeadParseResult parse_result =
        parseRequestHead(head, &method, &path, &headers);
    if (parse_result == HeadParseResult::DuplicateContentLength) {
        replyError(socket, 400, QStringLiteral("duplicate Content-Length header"));
        return;
    }
    if (parse_result != HeadParseResult::Ok) {
        replyError(socket, 400, QStringLiteral("malformed HTTP request"));
        return;
    }

    const bool is_post = method.compare("POST", Qt::CaseInsensitive) == 0;
    const auto content_length_it = headers.find("content-length");
    const bool has_content_length = content_length_it != headers.end();

    qint64 content_length = 0;
    if (has_content_length) {
        bool ok = false;
        content_length = content_length_it.value().toLongLong(&ok);
        if (!ok || content_length < 0) {
            replyError(socket, 400, QStringLiteral("invalid Content-Length"));
            return;
        }
        if (content_length > kMaxBodyBytes) {
            replyError(socket, 413, QStringLiteral("request body too large"));
            return;
        }
    }

    QByteArray body;
    if (is_post) {
        if (!has_content_length) {
            replyError(socket, 411, QStringLiteral("Content-Length is required"));
            return;
        }
        if (trailing.size() < content_length) return;  // wait for the full body
        body = trailing.left(static_cast<int>(content_length));
    }

    const int query = path.indexOf('?');
    const QString route = query >= 0 ? path.left(query) : path;

    if (method.compare("GET", Qt::CaseInsensitive) == 0 && route == QStringLiteral("/v1/models")) {
        handleListModels(socket);
        return;
    }
    if (is_post && route == QStringLiteral("/v1/chat/completions")) {
        const auto content_type = headers.find("content-type");
        if (content_type == headers.end() ||
            !content_type.value().toLower().contains("application/json")) {
            replyError(socket, 400, QStringLiteral("Content-Type must be application/json"));
            return;
        }
        handleChatCompletions(socket, body);
        return;
    }

    replyError(socket, 404, QStringLiteral("Not Found"));
}

void LocalApiService::handleRequestTimeout(QTcpSocket *socket) {
    if (!clients_.contains(socket)) return;
    const auto pending = pending_requests_.find(socket);
    // The read-phase timer is stopped on dispatch/reply, so a firing timer
    // means an unfinished request that exceeded its read deadline. Already
    // responded connections (pending == 0) are being closed anyway.
    if (pending != pending_requests_.end() && pending.value() == 0) return;
    replyError(socket, 408, QStringLiteral("request timed out"));
}

void LocalApiService::handleDisconnected(QTcpSocket *socket) {
    if (!clients_.contains(socket)) return;
    discardSocket(socket);
}

void LocalApiService::discardSocket(QTcpSocket *socket) {
    clients_.remove(socket);
    const auto pending = pending_requests_.find(socket);
    if (pending != pending_requests_.end()) {
        if (pending.value() != 0) {
            inference_service_->cancelApiChat(pending.value());
            if (outstanding_requests_ > 0) --outstanding_requests_;
        }
        pending_requests_.erase(pending);
    }
    read_buffers_.remove(socket);
    stopRequestTimer(socket);
    socket->abort();
    socket->deleteLater();
}

void LocalApiService::stopRequestTimer(QTcpSocket *socket) {
    const auto timer = request_timers_.find(socket);
    if (timer != request_timers_.end()) {
        timer.value()->stop();
        timer.value()->deleteLater();
        request_timers_.erase(timer);
    }
}

void LocalApiService::handleListModels(QTcpSocket *socket) {
    std::string loaded_model_id;
    bool supports_conversation = false;
    const bool ready =
        inference_service_->apiModelSnapshot(&loaded_model_id, &supports_conversation);

    // Only models that can accept ConversationInput are advertised; a Ready
    // model that is translation-only yields an empty list.
    QJsonArray data;
    if (ready && supports_conversation) {
        QJsonObject entry;
        entry.insert(QStringLiteral("id"), QString::fromStdString(loaded_model_id));
        entry.insert(QStringLiteral("object"), QStringLiteral("model"));
        entry.insert(QStringLiteral("created"), static_cast<qint64>(QDateTime::currentSecsSinceEpoch()));
        entry.insert(QStringLiteral("owned_by"), QStringLiteral("qtrans"));
        data.append(entry);
    }

    QJsonObject root;
    root.insert(QStringLiteral("object"), QStringLiteral("list"));
    root.insert(QStringLiteral("data"), data);
    replyJson(socket, 200, QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void LocalApiService::handleChatCompletions(QTcpSocket *socket, const QByteArray &body) {
    QJsonParseError parse_error;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
        replyError(socket, 400,
                   QStringLiteral("invalid JSON body: %1").arg(parse_error.errorString()));
        return;
    }
    const QJsonObject root = doc.object();

    for (const QString &key : root.keys()) {
        if (!supported_request_fields().contains(key)) {
            replyError(socket, 400, QStringLiteral("unsupported field: %1").arg(key));
            return;
        }
    }

    const QJsonValue model_value = root.value(QStringLiteral("model"));
    if (!model_value.isString() || model_value.toString().isEmpty()) {
        replyError(socket, 400, QStringLiteral("the 'model' field is required"));
        return;
    }
    const QString requested_model = model_value.toString();

    std::string loaded_model_id;
    bool supports_conversation = false;
    if (!inference_service_->apiModelSnapshot(&loaded_model_id, &supports_conversation)) {
        replyError(socket, 503, QStringLiteral("no model is currently loaded"));
        return;
    }
    if (requested_model != QString::fromStdString(loaded_model_id)) {
        replyError(socket, 404,
                   QStringLiteral("The model `%1` does not exist or you do not have access to it.")
                       .arg(requested_model));
        return;
    }
    if (!supports_conversation) {
        // Ready and matching, but the loaded model's prompt profile rejects
        // ConversationInput; refuse before submitting any model work.
        replyError(socket, 400,
                   QStringLiteral("the loaded model does not support conversation input"));
        return;
    }

    const QJsonValue messages_value = root.value(QStringLiteral("messages"));
    if (!messages_value.isArray() || messages_value.toArray().isEmpty()) {
        replyError(socket, 400, QStringLiteral("'messages' must be a nonempty array"));
        return;
    }
    const QJsonArray message_array = messages_value.toArray();
    if (message_array.size() > kMaxMessages) {
        replyError(socket, 400,
                   QStringLiteral("'messages' must contain at most %1 messages")
                       .arg(kMaxMessages));
        return;
    }
    std::vector<qtrans::core::Message> messages;
    messages.reserve(static_cast<std::size_t>(message_array.size()));
    for (const QJsonValue &message_value : message_array) {
        if (!message_value.isObject()) {
            replyError(socket, 400, QStringLiteral("each message must be an object"));
            return;
        }
        const QJsonObject message = message_value.toObject();
        for (const QString &key : message.keys()) {
            if (key != QStringLiteral("role") && key != QStringLiteral("content")) {
                replyError(socket, 400, QStringLiteral("unsupported message field: %1").arg(key));
                return;
            }
        }
        const QString role = message.value(QStringLiteral("role")).toString();
        qtrans::core::Role core_role;
        if (role == QStringLiteral("system")) {
            core_role = qtrans::core::Role::System;
        } else if (role == QStringLiteral("user")) {
            core_role = qtrans::core::Role::User;
        } else if (role == QStringLiteral("assistant")) {
            core_role = qtrans::core::Role::Assistant;
        } else {
            replyError(socket, 400, QStringLiteral("unsupported message role: %1").arg(role));
            return;
        }
        const QJsonValue content_value = message.value(QStringLiteral("content"));
        if (!content_value.isString()) {
            replyError(socket, 400,
                       QStringLiteral("message content must be a string (multimodal content is not supported)"));
            return;
        }
        const QByteArray content_bytes = content_value.toString().toUtf8();
        if (content_bytes.size() > kMaxMessageBytes) {
            replyError(socket, 400,
                       QStringLiteral("message content exceeds the %1 byte limit")
                           .arg(kMaxMessageBytes));
            return;
        }
        messages.push_back({core_role, qtrans::app::to_utf8(content_value.toString())});
    }

    const QJsonValue stream_value = root.value(QStringLiteral("stream"));
    if (!stream_value.isUndefined()) {
        if (!stream_value.isBool()) {
            replyError(socket, 400, QStringLiteral("'stream' must be a boolean"));
            return;
        }
        if (stream_value.toBool()) {
            replyError(socket, 400, QStringLiteral("streaming is not supported"));
            return;
        }
    }

    const QJsonValue n_value = root.value(QStringLiteral("n"));
    if (!n_value.isUndefined()) {
        if (!n_value.isDouble()) {
            replyError(socket, 400, QStringLiteral("'n' must be an integer"));
            return;
        }
        if (n_value.toDouble() != 1.0) {
            replyError(socket, 400, QStringLiteral("'n' must be 1"));
            return;
        }
    }

    if (root.contains(QStringLiteral("tools")) || root.contains(QStringLiteral("tool_choice"))) {
        replyError(socket, 400, QStringLiteral("tools and tool_choice are not supported"));
        return;
    }

    InferenceService::ApiChatRequest request;
    request.model_id = loaded_model_id;
    request.messages = std::move(messages);

    const QJsonValue temperature_value = root.value(QStringLiteral("temperature"));
    if (!temperature_value.isUndefined()) {
        if (!temperature_value.isDouble()) {
            replyError(socket, 400, QStringLiteral("'temperature' must be a number"));
            return;
        }
        const double temperature = temperature_value.toDouble();
        // Reject, rather than clamp, values outside the supported range.
        if (!std::isfinite(temperature) || temperature < 0.0 || temperature > 2.0) {
            replyError(socket, 400,
                       QStringLiteral("'temperature' must be a finite number in [0, 2]"));
            return;
        }
        request.temperature = static_cast<float>(temperature);
    }

    const QJsonValue top_p_value = root.value(QStringLiteral("top_p"));
    if (!top_p_value.isUndefined()) {
        if (!top_p_value.isDouble()) {
            replyError(socket, 400, QStringLiteral("'top_p' must be a number"));
            return;
        }
        const double top_p = top_p_value.toDouble();
        if (!std::isfinite(top_p) || top_p < 0.0 || top_p > 1.0) {
            replyError(socket, 400,
                       QStringLiteral("'top_p' must be a finite number in [0, 1]"));
            return;
        }
        request.top_p = static_cast<float>(top_p);
    }

    const QJsonValue seed_value = root.value(QStringLiteral("seed"));
    if (!seed_value.isUndefined()) {
        if (!seed_value.isDouble() || seed_value.toDouble() < 0.0 ||
            seed_value.toDouble() != std::floor(seed_value.toDouble())) {
            replyError(socket, 400, QStringLiteral("'seed' must be a non-negative integer"));
            return;
        }
        if (seed_value.toDouble() > 4294967295.0) {
            replyError(socket, 400, QStringLiteral("'seed' must fit in 32 bits"));
            return;
        }
        request.seed = static_cast<std::uint32_t>(seed_value.toDouble());
    }

    const QJsonValue max_tokens_value = root.value(QStringLiteral("max_tokens"));
    const QJsonValue max_completion_tokens_value =
        root.value(QStringLiteral("max_completion_tokens"));
    if (!max_tokens_value.isUndefined() && !max_completion_tokens_value.isUndefined()) {
        replyError(socket, 400,
                   QStringLiteral("max_tokens and max_completion_tokens cannot be used together"));
        return;
    }
    const QJsonValue max_output = max_tokens_value.isUndefined() ? max_completion_tokens_value
                                                                 : max_tokens_value;
    if (!max_output.isUndefined()) {
        if (!max_output.isDouble() || max_output.toDouble() < 1.0 ||
            max_output.toDouble() != std::floor(max_output.toDouble()) ||
            max_output.toDouble() > 4294967295.0) {
            replyError(socket, 400, QStringLiteral("'max_tokens' must be a positive integer"));
            return;
        }
        request.max_output_tokens = static_cast<std::uint32_t>(max_output.toDouble());
    }

    // Bounded outstanding model work: once the admitted-request cap is reached
    // new chat requests are rejected immediately.
    if (outstanding_requests_ >= kMaxOutstandingRequests) {
        replyError(socket, 429, QStringLiteral("too many concurrent chat requests"),
                   QStringLiteral("rate_limit_error"));
        return;
    }
    ++outstanding_requests_;

    const QString model_for_response = QString::fromStdString(loaded_model_id);
    const QPointer<QTcpSocket> socket_guard(socket);
    std::uint64_t request_id = 0;
    request_id = inference_service_->submitApiChat(
        request, [this, socket_guard, model_for_response](
                     const InferenceService::ApiChatReply &reply) {
            QMetaObject::invokeMethod(this, [this, socket_guard, model_for_response, reply] {
                if (!socket_guard) return;
                onApiChatFinished(socket_guard.data(), model_for_response, reply); }, Qt::QueuedConnection);
        });
    pending_requests_.insert(socket, request_id);
    // The read phase is complete; generation is bounded by the model-side
    // deadline, not the request-read timer.
    stopRequestTimer(socket);
}

void LocalApiService::onApiChatFinished(QTcpSocket *socket, const QString &model,
                                        const InferenceService::ApiChatReply &reply) {
    if (!clients_.contains(socket)) return;
    const auto pending = pending_requests_.find(socket);
    if (pending == pending_requests_.end()) return;

    pending_requests_.erase(pending);
    if (outstanding_requests_ > 0) --outstanding_requests_;

    const qtrans::core::Failure *failure = nullptr;
    if (!reply.accepted) {
        failure = &reply.failure;
    } else if (reply.result.failure) {
        failure = &*reply.result.failure;
    }
    if (failure != nullptr) {
        QString message = QString::fromStdString(failure->message);
        if (message.trimmed().isEmpty()) message = QStringLiteral("model request failed");
        int status = statusForFailure(*failure);
        if (reply.result.finish_reason == qtrans::core::FinishReason::Preempted ||
            failure->code == qtrans::core::FailureCode::Backpressure) {
            status = 429;
        }
        const QString type = status == 429
                                 ? QStringLiteral("rate_limit_error")
                                 : (status >= 500 ? QStringLiteral("server_error")
                                                  : QStringLiteral("invalid_request_error"));
        replyError(socket, status, message, type);
        return;
    }

    const QString finish_reason =
        reply.result.finish_reason == qtrans::core::FinishReason::Length
            ? QStringLiteral("length")
            : QStringLiteral("stop");
    const QString id =
        QStringLiteral("chatcmpl-%1").arg(static_cast<qulonglong>(reply.result.id.value));
    const QByteArray json =
        completionJson(model, id, qtrans::app::from_utf8(reply.result.output), finish_reason,
                       reply.result.usage);
    replyJson(socket, 200, json);
}

void LocalApiService::replyJson(QTcpSocket *socket, int status, const QByteArray &json) {
    stopRequestTimer(socket);

    QByteArray response;
    response += "HTTP/1.1 ";
    response += QByteArray::number(status);
    response += ' ';
    response += statusPhrase(status);
    response += "\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Content-Length: ";
    response += QByteArray::number(json.size());
    response += "\r\n";
    response += "Connection: close\r\n";
    response += "\r\n";
    response += json;

    // Mark the connection as responded BEFORE initiating the close:
    // disconnectFromHost() can synchronously emit disconnected(), which runs
    // discardSocket() and erases this entry; inserting after that would leave
    // a stale pointer-keyed entry that a future socket (reusing the address)
    // would trip over.
    pending_requests_.insert(socket, 0);
    socket->write(response);
    socket->flush();
    // Ensure the socket is reclaimed even if the peer never closes after our
    // Connection: close response.
    QPointer<QTcpSocket> guard(socket);
    QTimer::singleShot(10000, socket, [guard] {
        if (guard) guard->abort();
    });
    socket->disconnectFromHost();
}

void LocalApiService::replyError(QTcpSocket *socket, int status, const QString &message,
                                 const QString &type) {
    QJsonObject error_object;
    error_object.insert(QStringLiteral("message"), message);
    error_object.insert(QStringLiteral("type"), type);
    error_object.insert(QStringLiteral("param"), QJsonValue(QJsonValue::Null));
    error_object.insert(QStringLiteral("code"), QJsonValue(QJsonValue::Null));
    QJsonObject root;
    root.insert(QStringLiteral("error"), error_object);
    replyJson(socket, status, QJsonDocument(root).toJson(QJsonDocument::Compact));
}

int LocalApiService::statusForFailure(const qtrans::core::Failure &failure) {
    switch (failure.code) {
        case qtrans::core::FailureCode::NotLoaded:
        case qtrans::core::FailureCode::LifecycleTransition:
        case qtrans::core::FailureCode::Shutdown:
            return 503;
        case qtrans::core::FailureCode::Deadline:
            return 504;
        case qtrans::core::FailureCode::Backpressure:
            return 429;
        case qtrans::core::FailureCode::ContextLimit:
        case qtrans::core::FailureCode::InvalidRequest:
        case qtrans::core::FailureCode::UnsupportedModel:
        case qtrans::core::FailureCode::UnsupportedCapability:
        case qtrans::core::FailureCode::Cancelled:
            return 400;
        case qtrans::core::FailureCode::Runtime:
        case qtrans::core::FailureCode::Observer:
        case qtrans::core::FailureCode::AlreadyExists:
        case qtrans::core::FailureCode::None:
            return 500;
    }
    return 500;
}

QByteArray LocalApiService::completionJson(const QString &model, const QString &id,
                                           const QString &content, const QString &finish_reason,
                                           const qtrans::core::TokenUsage &usage) {
    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("assistant"));
    message.insert(QStringLiteral("content"), content);

    QJsonObject choice;
    choice.insert(QStringLiteral("index"), 0);
    choice.insert(QStringLiteral("message"), message);
    choice.insert(QStringLiteral("finish_reason"), finish_reason);

    QJsonObject usage_object;
    usage_object.insert(QStringLiteral("prompt_tokens"), static_cast<qint64>(usage.input_tokens));
    usage_object.insert(QStringLiteral("completion_tokens"),
                        static_cast<qint64>(usage.output_tokens));
    usage_object.insert(QStringLiteral("total_tokens"), static_cast<qint64>(usage.total_tokens));

    QJsonArray choices;
    choices.append(choice);

    QJsonObject root;
    root.insert(QStringLiteral("id"), id);
    root.insert(QStringLiteral("object"), QStringLiteral("chat.completion"));
    root.insert(QStringLiteral("created"), static_cast<qint64>(QDateTime::currentSecsSinceEpoch()));
    root.insert(QStringLiteral("model"), model);
    root.insert(QStringLiteral("choices"), choices);
    root.insert(QStringLiteral("usage"), usage_object);

    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}
