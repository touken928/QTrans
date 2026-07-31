#pragma once

#include "domain/download/download_types.h"
#include "domain/download/download_cancellation.h"

#include <cstdint>
#include <functional>
#include <string>

enum class ExecutionOutcome { Completed,
                              Cancelled,
                              Failed };

struct ExecutionResult {
    ExecutionOutcome outcome = ExecutionOutcome::Failed;
    std::string error_message;
};

struct DownloadProgressData {
    std::int64_t downloaded_bytes = 0;
    std::int64_t total_bytes = 0;
    double speed_bps = 0.0;
    double eta_seconds = -1.0;
};

using DownloadProgressHandler = std::function<void(const DownloadProgressData &)>;

class IModelDownloader {
public:
    virtual ~IModelDownloader() = default;
    virtual ExecutionResult download(const DownloadRequest &, const DownloadCancelToken *, DownloadProgressHandler) = 0;
};

class ProductionModelDownloader final : public IModelDownloader {
public:
    ExecutionResult download(
        const DownloadRequest &request,
        const DownloadCancelToken *cancel_token,
        DownloadProgressHandler on_progress) override;
};
