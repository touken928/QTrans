#pragma once

#include "qtrans/core.h"

#include <memory>
#include <string>

namespace qtrans::core {

class LocalRuntime {
public:
    explicit LocalRuntime(BackendState environment);
    ~LocalRuntime();

    LocalRuntime(const LocalRuntime &) = delete;
    LocalRuntime &operator=(const LocalRuntime &) = delete;
    LocalRuntime(LocalRuntime &&) noexcept;
    LocalRuntime &operator=(LocalRuntime &&) noexcept;

    void load(const Model &model);
    void unload() noexcept;
    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] Backend backend() const noexcept;

    std::string translate(const std::string &prompt,
                          const TokenSink &on_token = nullptr,
                          const StopPredicate &should_cancel = nullptr);
    [[nodiscard]] int count_prompt_tokens(const std::string &prompt) const;

private:
    std::string generate(const std::string &prompt,
                         const TokenSink &on_token,
                         const StopPredicate &should_cancel);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace qtrans::core
