// Tests for the local OpenAI-compatible HTTP API service (LocalApiService).
//
// All requests use real QTcpSocket loopback traffic against a server bound to
// 127.0.0.1 on an ephemeral port. The InferenceService runs with the test
// ModelHost hooks installed and never loads a real model: the default test
// runtime keeps the host unloaded (GET /v1/models is empty, chat is 503) and,
// for the ordering tests, a "demo" model is loaded through the test hooks so
// the ready-state validation paths are exercised without any download.

#include "app/inference_service.h"
#include "app/local_api_service.h"
#include "model_host_test_access.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>

#include <chrono>
#include <functional>
#include <memory>
#include <thread>

namespace {

constexpr auto kEventTimeout = std::chrono::seconds(5);

void process_until(QCoreApplication &application, const std::function<bool()> &condition) {
    const auto deadline = std::chrono::steady_clock::now() + kEventTimeout;
    while (!condition() && std::chrono::steady_clock::now() < deadline) {
        application.processEvents(QEventLoop::AllEvents, 10);
        std::this_thread::yield();
    }
}

struct HttpResponse {
    int status = 0;
    QHash<QByteArray, QByteArray> headers;
    QByteArray body;
};

// True once `data` holds a full HTTP response: the header terminator plus the
// declared Content-Length bytes. The local API always sends Content-Length.
bool response_complete(const QByteArray &data) {
    const int header_end = data.indexOf("\r\n\r\n");
    if (header_end < 0) return false;
    const QByteArray head = data.left(header_end);
    const int pos = head.toLower().indexOf("content-length:");
    if (pos < 0) return false;
    const int line_end = head.indexOf("\r\n", pos);
    if (line_end < 0) return false;
    const QByteArray value = head.mid(pos + 15, line_end - (pos + 15)).trimmed();
    bool ok = false;
    const int length = value.toInt(&ok);
    if (!ok || length < 0) return false;
    return data.size() >= header_end + 4 + length;
}

// Sends a raw request over a fresh loopback connection and returns the parsed
// response once the server answers (the server closes every connection).
HttpResponse send_request(QCoreApplication &application, quint16 port,
                          const QByteArray &request) {
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    process_until(application,
                  [&] { return socket.state() == QAbstractSocket::ConnectedState; });
    if (socket.state() != QAbstractSocket::ConnectedState) return {};
    socket.write(request);
    socket.flush();

    QByteArray received;
    process_until(application, [&] {
        received.append(socket.readAll());
        return response_complete(received) ||
               socket.state() == QAbstractSocket::UnconnectedState;
    });
    received.append(socket.readAll());

    HttpResponse response;
    const int header_end = received.indexOf("\r\n\r\n");
    if (header_end < 0) return response;
    const QByteArray head = received.left(header_end);
    const QList<QByteArray> lines = head.split('\n');
    if (!lines.isEmpty()) {
        const QList<QByteArray> status_parts = lines.at(0).trimmed().split(' ');
        if (status_parts.size() >= 2) response.status = status_parts.at(1).toInt();
    }
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int colon = line.indexOf(':');
        if (colon > 0) {
            response.headers.insert(line.left(colon).trimmed().toLower(),
                                    line.mid(colon + 1).trimmed());
        }
    }
    response.body = received.mid(header_end + 4);
    return response;
}

QByteArray make_request(const QByteArray &method, const QByteArray &path,
                        const QByteArray &content_type, const QByteArray &body) {
    QByteArray request;
    request += method + " " + path + " HTTP/1.1\r\n";
    request += "Host: 127.0.0.1\r\n";
    if (!content_type.isEmpty()) request += "Content-Type: " + content_type + "\r\n";
    if (method.compare("POST", Qt::CaseInsensitive) == 0) {
        request += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    }
    request += "Connection: close\r\n";
    request += "\r\n";
    request += body;
    return request;
}

QByteArray chat_body(const char *model, bool stream = false) {
    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("user"));
    message.insert(QStringLiteral("content"), QStringLiteral("hello"));

    QJsonArray messages;
    messages.append(message);

    QJsonObject root;
    root.insert(QStringLiteral("model"), QString::fromUtf8(model));
    root.insert(QStringLiteral("messages"), messages);
    if (stream) root.insert(QStringLiteral("stream"), true);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray system_user_chat_body(const char *model) {
    QJsonObject system_message;
    system_message.insert(QStringLiteral("role"), QStringLiteral("system"));
    system_message.insert(QStringLiteral("content"),
                          QStringLiteral("You are a concise translator."));

    QJsonObject user_message;
    user_message.insert(QStringLiteral("role"), QStringLiteral("user"));
    user_message.insert(QStringLiteral("content"),
                        QStringLiteral("Translate hello into French."));

    QJsonArray messages;
    messages.append(system_message);
    messages.append(user_message);

    QJsonObject root;
    root.insert(QStringLiteral("model"), QString::fromUtf8(model));
    root.insert(QStringLiteral("messages"), messages);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

}  // namespace

class LocalApiServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        static char name[] = "local-api-service-test";
        argv_[0] = name;
        argv_[1] = nullptr;
        argc_ = 1;
        application_ = std::make_unique<QCoreApplication>(argc_, argv_);
        // Hooks keep the core from probing a real backend while still
        // allowing official models (hymt2-q4 / hymt2-7b-q4) to be loaded for
        // the ready-state tests.
        hooks_ = std::make_unique<qtrans::core::test::ScopedModelHostHooks>(
            qtrans::core::test::ModelHostHooks{});
        service_ = std::make_unique<InferenceService>();
        api_ = std::make_unique<LocalApiService>(service_.get());
    }

    void TearDown() override {
        api_->stop();
        service_->shutdown();
        api_.reset();
        service_.reset();
        hooks_.reset();
        application_.reset();
    }

    // Loads an official model through the test hooks and waits for the
    // terminal modelLoadFinished signal.
    bool loadModel(const QString &model_id) {
        bool loaded = false;
        QObject::connect(service_.get(), &InferenceService::modelLoadFinished,
                         application_.get(),
                         [&loaded](bool success, const QString &, const QString &) {
                             loaded = success;
                         });
        service_->setModelConfig(model_id, QString());
        service_->loadModel();
        process_until(*application_, [&] { return loaded; });
        return loaded;
    }

    int argc_ = 0;
    char *argv_[2] = {nullptr, nullptr};
    std::unique_ptr<QCoreApplication> application_;
    std::unique_ptr<qtrans::core::test::ScopedModelHostHooks> hooks_;
    std::unique_ptr<InferenceService> service_;
    std::unique_ptr<LocalApiService> api_;
};

TEST_F(LocalApiServiceTest, StartAndStopAreIdempotent) {
    ASSERT_TRUE(api_->start(0));
    const quint16 port = api_->port();
    ASSERT_NE(port, 0);
    EXPECT_TRUE(api_->isListening());

    QString error;
    EXPECT_TRUE(api_->start(port, &error));  // already listening on this port
    EXPECT_TRUE(api_->isListening());
    EXPECT_EQ(api_->port(), port);

    api_->stop();
    EXPECT_FALSE(api_->isListening());
    EXPECT_EQ(api_->port(), 0);

    api_->stop();  // stopping an already stopped server is a no-op
    EXPECT_FALSE(api_->isListening());

    ASSERT_TRUE(api_->start(0));  // restartable after stop
    EXPECT_TRUE(api_->isListening());
    EXPECT_NE(api_->port(), 0);
    api_->stop();
}

TEST_F(LocalApiServiceTest, ListModelsReturns200AndEmptyDataWhenUnloaded) {
    ASSERT_TRUE(api_->start(0));
    const HttpResponse response =
        send_request(*application_, api_->port(), make_request("GET", "/v1/models", {}, {}));
    ASSERT_EQ(response.status, 200);
    EXPECT_EQ(response.headers.value("content-type"), "application/json");

    QJsonParseError parse_error;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError);
    ASSERT_TRUE(doc.isObject());
    const QJsonObject root = doc.object();
    EXPECT_EQ(root.value(QStringLiteral("object")).toString(), QStringLiteral("list"));
    ASSERT_TRUE(root.value(QStringLiteral("data")).isArray());
    EXPECT_TRUE(root.value(QStringLiteral("data")).toArray().isEmpty());
}

TEST_F(LocalApiServiceTest, ChatCompletionsReturns503WhenUnloaded) {
    ASSERT_TRUE(api_->start(0));
    const HttpResponse response = send_request(
        *application_, api_->port(),
        make_request("POST", "/v1/chat/completions", "application/json",
                     chat_body("demo")));
    ASSERT_EQ(response.status, 503);

    QJsonParseError parse_error;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError);
    const QJsonObject error_object = doc.object().value(QStringLiteral("error")).toObject();
    EXPECT_EQ(error_object.value(QStringLiteral("message")).toString(),
              QStringLiteral("no model is currently loaded"));
}

TEST_F(LocalApiServiceTest, MalformedHttpRequestIsRejected) {
    ASSERT_TRUE(api_->start(0));
    // Request line without a method/path/version triple.
    const HttpResponse malformed = send_request(*application_, api_->port(), "GARBAGE\r\n\r\n");
    EXPECT_EQ(malformed.status, 400);

    // Unknown routes are 404, not crashes.
    const HttpResponse not_found =
        send_request(*application_, api_->port(), make_request("GET", "/v1/unknown", {}, {}));
    EXPECT_EQ(not_found.status, 404);

    // GET on the chat route is 404 (route requires POST).
    const HttpResponse wrong_method =
        send_request(*application_, api_->port(), make_request("GET", "/v1/chat/completions", {}, {}));
    EXPECT_EQ(wrong_method.status, 404);
}

TEST_F(LocalApiServiceTest, NonJsonBodyIsRejected) {
    ASSERT_TRUE(api_->start(0));
    const HttpResponse response = send_request(
        *application_, api_->port(),
        make_request("POST", "/v1/chat/completions", "application/json",
                     "this is not json"));
    EXPECT_EQ(response.status, 400);
}

TEST_F(LocalApiServiceTest, MissingOrWrongContentTypeIsRejected) {
    ASSERT_TRUE(api_->start(0));
    const QByteArray body = chat_body("demo");

    const HttpResponse missing = send_request(
        *application_, api_->port(),
        make_request("POST", "/v1/chat/completions", QByteArray(), body));
    EXPECT_EQ(missing.status, 400);

    const HttpResponse wrong = send_request(
        *application_, api_->port(),
        make_request("POST", "/v1/chat/completions", "text/plain", body));
    EXPECT_EQ(wrong.status, 400);
}

TEST_F(LocalApiServiceTest, UnloadedStateShortCircuitsBeforeFieldValidation) {
    ASSERT_TRUE(api_->start(0));

    // stream=true with an otherwise valid body: the unloaded check must win
    // (503), not the stream validation (400).
    const HttpResponse streamed = send_request(
        *application_, api_->port(),
        make_request("POST", "/v1/chat/completions", "application/json",
                     chat_body("demo", /*stream=*/true)));
    EXPECT_EQ(streamed.status, 503);

    // Unknown model: the unloaded check must win (503), not the model lookup
    // (404).
    const HttpResponse unknown_model = send_request(
        *application_, api_->port(),
        make_request("POST", "/v1/chat/completions", "application/json",
                     chat_body("no-such-model")));
    EXPECT_EQ(unknown_model.status, 503);
}

TEST_F(LocalApiServiceTest, LoadedStateValidatesStreamAndModelOrdering) {
    ASSERT_TRUE(loadModel(QStringLiteral("hymt2-q4")));
    ASSERT_TRUE(api_->start(0));

    // The loaded 1.8B model is now listed by /v1/models.
    const HttpResponse list = send_request(*application_, api_->port(),
                                           make_request("GET", "/v1/models", {}, {}));
    ASSERT_EQ(list.status, 200);
    QJsonParseError parse_error;
    const QJsonDocument list_doc = QJsonDocument::fromJson(list.body, &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError);
    const QJsonArray data = list_doc.object().value(QStringLiteral("data")).toArray();
    ASSERT_EQ(data.size(), 1);
    EXPECT_EQ(data.at(0).toObject().value(QStringLiteral("id")).toString(),
              QStringLiteral("hymt2-q4"));

    // stream=true is validated only once the model is ready: 400, not 503.
    const HttpResponse streamed = send_request(
        *application_, api_->port(),
        make_request("POST", "/v1/chat/completions", "application/json",
                     chat_body("hymt2-q4", /*stream=*/true)));
    EXPECT_EQ(streamed.status, 400);

    // Wrong model is validated after readiness: 404, not 503.
    const HttpResponse wrong_model = send_request(
        *application_, api_->port(),
        make_request("POST", "/v1/chat/completions", "application/json",
                     chat_body("other-model")));
    EXPECT_EQ(wrong_model.status, 404);
}

TEST_F(LocalApiServiceTest, ChatCompletionsSucceedWhenModelLoaded) {
    ASSERT_TRUE(loadModel(QStringLiteral("hymt2-q4")));
    ASSERT_TRUE(api_->start(0));

    const HttpResponse response = send_request(
        *application_, api_->port(),
        make_request("POST", "/v1/chat/completions", "application/json",
                     chat_body("hymt2-q4")));
    ASSERT_EQ(response.status, 200);

    QJsonParseError parse_error;
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError);
    ASSERT_TRUE(doc.isObject());
    const QJsonObject root = doc.object();
    EXPECT_EQ(root.value(QStringLiteral("object")).toString(),
              QStringLiteral("chat.completion"));
    EXPECT_EQ(root.value(QStringLiteral("model")).toString(), QStringLiteral("hymt2-q4"));
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    ASSERT_EQ(choices.size(), 1);
    EXPECT_EQ(choices.at(0).toObject().value(QStringLiteral("finish_reason")).toString(),
              QStringLiteral("stop"));
}

TEST_F(LocalApiServiceTest, SevenBConversationIsAdvertisedAndCompletes) {
    ASSERT_TRUE(loadModel(QStringLiteral("hymt2-7b-q4")));
    ASSERT_TRUE(api_->start(0));

    // GET /v1/models advertises the loaded 7B model.
    const HttpResponse list = send_request(*application_, api_->port(),
                                           make_request("GET", "/v1/models", {}, {}));
    ASSERT_EQ(list.status, 200);
    QJsonParseError parse_error;
    const QJsonDocument list_doc = QJsonDocument::fromJson(list.body, &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError);
    const QJsonArray data = list_doc.object().value(QStringLiteral("data")).toArray();
    ASSERT_EQ(data.size(), 1);
    EXPECT_EQ(data.at(0).toObject().value(QStringLiteral("id")).toString(),
              QStringLiteral("hymt2-7b-q4"));

    // A system/user conversation completes successfully through the official
    // 7B multi-turn template.
    const HttpResponse response = send_request(
        *application_, api_->port(),
        make_request("POST", "/v1/chat/completions", "application/json",
                     system_user_chat_body("hymt2-7b-q4")));
    ASSERT_EQ(response.status, 200);
    const QJsonDocument doc = QJsonDocument::fromJson(response.body, &parse_error);
    ASSERT_EQ(parse_error.error, QJsonParseError::NoError);
    ASSERT_TRUE(doc.isObject());
    const QJsonObject root = doc.object();
    EXPECT_EQ(root.value(QStringLiteral("object")).toString(),
              QStringLiteral("chat.completion"));
    EXPECT_EQ(root.value(QStringLiteral("model")).toString(),
              QStringLiteral("hymt2-7b-q4"));
    const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
    ASSERT_EQ(choices.size(), 1);
    EXPECT_EQ(choices.at(0).toObject().value(QStringLiteral("finish_reason")).toString(),
              QStringLiteral("stop"));
}

TEST_F(LocalApiServiceTest, StartFailsWhenPortIsInUse) {
    // Occupy a loopback port with an independent listener.
    QTcpServer blocker;
    ASSERT_TRUE(blocker.listen(QHostAddress::LocalHost, 0));
    const quint16 busy_port = blocker.serverPort();

    QString error;
    EXPECT_FALSE(api_->start(busy_port, &error));
    EXPECT_FALSE(api_->isListening());
    EXPECT_FALSE(error.isEmpty());

    // Once the port is released the same port binds successfully.
    blocker.close();
    EXPECT_TRUE(api_->start(busy_port, &error));
    EXPECT_TRUE(api_->isListening());
    api_->stop();
}

TEST_F(LocalApiServiceTest, ResponsesDoNotIncludeCorsHeaders) {
    ASSERT_TRUE(api_->start(0));

    const HttpResponse list = send_request(*application_, api_->port(),
                                           make_request("GET", "/v1/models", {}, {}));
    ASSERT_EQ(list.status, 200);
    EXPECT_FALSE(list.headers.contains("access-control-allow-origin"));

    const HttpResponse error = send_request(
        *application_, api_->port(),
        make_request("POST", "/v1/chat/completions", "application/json",
                     chat_body("demo")));
    ASSERT_EQ(error.status, 503);
    EXPECT_FALSE(error.headers.contains("access-control-allow-origin"));
}

TEST_F(LocalApiServiceTest, OversizedHeaderWithoutTerminatorRejectedWith431) {
    ASSERT_TRUE(api_->start(0));
    // 70 KB with no header terminator exceeds kMaxHeaderBytes but stays below
    // the total request cap, so the header-limit path (431) is exercised.
    const QByteArray huge_header(70 * 1024, 'a');
    const HttpResponse response = send_request(*application_, api_->port(), huge_header);
    EXPECT_EQ(response.status, 431);
}

TEST_F(LocalApiServiceTest, DeclaredBodyLargerThanCapRejectedWith413) {
    ASSERT_TRUE(api_->start(0));
    QByteArray request;
    request += "POST /v1/chat/completions HTTP/1.1\r\n";
    request += "Host: 127.0.0.1\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: 2000000\r\n";  // exceeds the 1 MiB body cap
    request += "\r\n";
    const HttpResponse response = send_request(*application_, api_->port(), request);
    EXPECT_EQ(response.status, 413);
}

TEST_F(LocalApiServiceTest, DuplicateContentLengthRejectedWith400) {
    ASSERT_TRUE(api_->start(0));
    QByteArray request;
    request += "POST /v1/chat/completions HTTP/1.1\r\n";
    request += "Host: 127.0.0.1\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: 10\r\n";
    request += "Content-Length: 10\r\n";
    request += "\r\n";
    request += "{\"model\":\"x\"}";
    const HttpResponse response = send_request(*application_, api_->port(), request);
    EXPECT_EQ(response.status, 400);
}
