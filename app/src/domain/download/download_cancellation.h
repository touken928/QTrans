#pragma once

#include <atomic>
#include <functional>

// Shared cancellation control for a dedicated download run. Owned by
// DownloadService; observed by the download executor and curl callbacks.
class DownloadCancelToken {
public:
    void cancel() {
        cancelled_.store(true, std::memory_order_relaxed);
    }

    void reset() {
        cancelled_.store(false, std::memory_order_relaxed);
    }

    bool is_cancelled() const {
        return cancelled_.load(std::memory_order_relaxed);
    }

    std::function<bool()> checker() const {
        return [this]() { return cancelled_.load(std::memory_order_relaxed); };
    }

private:
    mutable std::atomic<bool> cancelled_{false};
};
