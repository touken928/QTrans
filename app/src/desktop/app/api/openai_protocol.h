#pragma once

#include "qtrans/core.h"

#include <QByteArray>
#include <QString>

namespace qtrans::app::openai {

int status_for_failure(const qtrans::core::Failure &failure);
QByteArray error_json(const QString &message, const QString &type);
QByteArray completion_json(const QString &model, const QString &id,
                           const QString &content,
                           const QString &finish_reason,
                           const qtrans::core::TokenUsage &usage);

}  // namespace qtrans::app::openai
