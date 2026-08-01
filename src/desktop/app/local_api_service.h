#pragma once

#include "app/inference_service.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include <cstdint>

class QTcpServer;
class QTcpSocket;
class QTimer;

// Serves a limited, local OpenAI-compatible HTTP API on the loopback IPv4
// interface (127.0.0.1 only — never binds non-loopback addresses). Lives on
// the UI/application thread: all QTcpServer / QTcpSocket work happens here and
// model work is delegated through InferenceService's thread-safe API bridge
// (ApiInteractive work class). The service never loads, unloads or otherwise
// changes the model.
//
// Capacity limits (a bounded local server, not a general-purpose HTTP front):
//   - kMaxClients:            max simultaneous TCP clients
//   - kMaxOutstandingRequests: max chat requests admitted to the model backend
//   - kRequestTimeoutMs:      read deadline for an unfinished HTTP request
//   - kMaxHeaderBytes / kMaxBodyBytes: request framing caps
//   - kMaxMessages / kMaxMessageBytes: chat payload caps
// Saturation and timeouts are surfaced as OpenAI-style JSON errors (429/408).
class LocalApiService : public QObject {
    Q_OBJECT

public:
    explicit LocalApiService(InferenceService *inference_service, QObject *parent = nullptr);
    ~LocalApiService() override;

    LocalApiService(const LocalApiService &) = delete;
    LocalApiService &operator=(const LocalApiService &) = delete;

    // Starts listening on 127.0.0.1:port. Idempotent: returns true when the
    // server is already listening on the requested port. On failure sets
    // error_message and returns false.
    bool start(quint16 port, QString *error_message = nullptr);
    // Stops listening and cancels/closes all in-flight clients. Idempotent.
    void stop();
    bool isListening() const;
    quint16 port() const;

private:
    static constexpr int kMaxClients = 64;
    static constexpr int kMaxOutstandingRequests = 8;
    static constexpr int kRequestTimeoutMs = 15000;
    static constexpr int kMaxHeaderBytes = 64 * 1024;
    static constexpr int kMaxBodyBytes = 1024 * 1024;
    static constexpr int kMaxTotalBytes = kMaxHeaderBytes + kMaxBodyBytes;
    static constexpr int kMaxMessages = 128;
    static constexpr int kMaxMessageBytes = 64 * 1024;

    void handleNewConnection();
    void handleReadyRead(QTcpSocket *socket);
    void handleRequestTimeout(QTcpSocket *socket);
    void handleDisconnected(QTcpSocket *socket);
    void discardSocket(QTcpSocket *socket);
    void stopRequestTimer(QTcpSocket *socket);

    void handleListModels(QTcpSocket *socket);
    void handleChatCompletions(QTcpSocket *socket, const QByteArray &body);

    void onApiChatFinished(QTcpSocket *socket, const QString &model,
                           const InferenceService::ApiChatReply &reply);

    void replyJson(QTcpSocket *socket, int status, const QByteArray &json);
    void replyError(QTcpSocket *socket, int status, const QString &message,
                    const QString &type = QStringLiteral("invalid_request_error"));

    static int statusForFailure(const qtrans::core::Failure &failure);
    static QByteArray completionJson(const QString &model, const QString &id,
                                     const QString &content, const QString &finish_reason,
                                     const qtrans::core::TokenUsage &usage);

    InferenceService *inference_service_ = nullptr;
    QTcpServer *server_ = nullptr;
    QSet<QTcpSocket *> clients_;
    QHash<QTcpSocket *, QByteArray> read_buffers_;
    QHash<QTcpSocket *, std::uint64_t> pending_requests_;
    QHash<QTcpSocket *, QTimer *> request_timers_;
    int outstanding_requests_ = 0;
};
