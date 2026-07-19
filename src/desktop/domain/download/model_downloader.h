#pragma once

#include "domain/tasks/task_execution.h"

class ProductionModelDownloader final : public IModelDownloader {
public:
    ExecutionResult download(
        const DownloadModelPayload &payload,
        const CancelToken *cancel_token,
        DownloadProgressHandler on_progress) override;
};
