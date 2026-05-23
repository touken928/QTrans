#pragma once

#include <atomic>
#include <functional>

class CancelToken {
public:
    void cancel() { cancelled_.store(true, std::memory_order_relaxed); }

    void reset() { cancelled_.store(false, std::memory_order_relaxed); }

    bool is_cancelled() const { return cancelled_.load(std::memory_order_relaxed); }

    std::function<bool()> checker() const {
        return [this]() { return cancelled_.load(std::memory_order_relaxed); };
    }

private:
    mutable std::atomic<bool> cancelled_{false};
};
