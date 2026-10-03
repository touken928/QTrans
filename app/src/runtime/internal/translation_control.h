#pragma once

#include <exception>
#include <functional>
#include <string>

namespace qtrans::core::runtime_detail {

class TranslationCancelled final : public std::exception {
public:
    const char *what() const noexcept override {
        return "translation cancelled";
    }
};

class TranslationCallbackFailed final : public std::exception {
public:
    explicit TranslationCallbackFailed(std::string message)
        : message_(std::move(message)) {
    }
    const char *what() const noexcept override {
        return message_.c_str();
    }

private:
    std::string message_;
};

struct CallbackState {
    std::function<bool()> checker;
    std::exception_ptr exception;

    bool requested() noexcept {
        if (exception) return true;
        if (!checker) return false;
        try {
            return checker();
        } catch (...) {
            exception = std::current_exception();
            return true;
        }
    }
};

inline void check_stop(CallbackState &state) {
    if (!state.requested()) return;
    if (state.exception) std::rethrow_exception(state.exception);
    throw TranslationCancelled();
}

}  // namespace qtrans::core::runtime_detail
