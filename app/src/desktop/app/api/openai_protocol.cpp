#include "app/api/openai_protocol.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace qtrans::app::openai {

int status_for_failure(const qtrans::core::Failure &failure) {
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

QByteArray error_json(const QString &message, const QString &type) {
    QJsonObject error;
    error.insert(QStringLiteral("message"), message);
    error.insert(QStringLiteral("type"), type);
    error.insert(QStringLiteral("param"), QJsonValue(QJsonValue::Null));
    error.insert(QStringLiteral("code"), QJsonValue(QJsonValue::Null));
    QJsonObject root;
    root.insert(QStringLiteral("error"), error);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray completion_json(const QString &model, const QString &id,
                           const QString &content,
                           const QString &finish_reason,
                           const qtrans::core::TokenUsage &usage) {
    QJsonObject message;
    message.insert(QStringLiteral("role"), QStringLiteral("assistant"));
    message.insert(QStringLiteral("content"), content);

    QJsonObject choice;
    choice.insert(QStringLiteral("index"), 0);
    choice.insert(QStringLiteral("message"), message);
    choice.insert(QStringLiteral("finish_reason"), finish_reason);

    QJsonObject usage_object;
    usage_object.insert(QStringLiteral("prompt_tokens"),
                        static_cast<qint64>(usage.input_tokens));
    usage_object.insert(QStringLiteral("completion_tokens"),
                        static_cast<qint64>(usage.output_tokens));
    usage_object.insert(QStringLiteral("total_tokens"),
                        static_cast<qint64>(usage.total_tokens));

    QJsonArray choices;
    choices.append(choice);
    QJsonObject root;
    root.insert(QStringLiteral("id"), id);
    root.insert(QStringLiteral("object"), QStringLiteral("chat.completion"));
    root.insert(QStringLiteral("created"),
                static_cast<qint64>(QDateTime::currentSecsSinceEpoch()));
    root.insert(QStringLiteral("model"), model);
    root.insert(QStringLiteral("choices"), choices);
    root.insert(QStringLiteral("usage"), usage_object);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

}  // namespace qtrans::app::openai
