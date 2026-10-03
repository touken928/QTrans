#include "domain/inference/inference_types.h"

bool TranslationCancellation::request() {
    std::function<void()> callback;
    {
        std::lock_guard lock(mutex_);
        if (completed_) return false;
        requested_ = true;
        callback = callback_;
    }
    if (callback) callback();
    return true;
}

bool TranslationCancellation::requested() const {
    std::lock_guard lock(mutex_);
    return requested_;
}

void TranslationCancellation::install(std::function<void()> callback) {
    bool replay = false;
    {
        std::lock_guard lock(mutex_);
        if (completed_) return;
        callback_ = std::move(callback);
        replay = requested_;
        callback = callback_;
    }
    if (replay && callback) callback();
}

void TranslationCancellation::complete() {
    std::lock_guard lock(mutex_);
    completed_ = true;
    callback_ = {};
}

bool TranslationJobTicket::cancel() const {
    return cancellation && cancellation->request();
}
