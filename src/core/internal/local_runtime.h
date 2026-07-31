#pragma once

#include "qtrans/core.h"
#include "runtime_types.h"

#include <memory>
#include <string>

namespace qtrans::core {

class LocalRuntime {
public:
    struct GenerationStats {
        int prompt_tokens = 0;
        int output_tokens = 0;
        bool stopped_at_end = false;
        bool reached_limit = false;
        std::string output_text;
    };

    explicit LocalRuntime(BackendState environment);
    ~LocalRuntime();

    LocalRuntime(const LocalRuntime &) = delete;
    LocalRuntime &operator=(const LocalRuntime &) = delete;
    LocalRuntime(LocalRuntime &&) noexcept;
    LocalRuntime &operator=(LocalRuntime &&) noexcept;

    void load(const runtime_detail::ModelLoad &model);
    void unload() noexcept;
    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] Backend backend() const noexcept;
    [[nodiscard]] int context_tokens() const noexcept;

    std::string translate_with_stats(const std::string &prompt,
                                     const runtime_detail::GenerationOptions &generation,
                                     const runtime_detail::TokenSink &on_token,
                                     const runtime_detail::StopPredicate &should_cancel,
                                     GenerationStats &stats);
    [[nodiscard]] int count_prompt_tokens(const std::string &prompt) const;

private:
    std::string generate(const std::string &prompt,
                         const runtime_detail::GenerationOptions &generation,
                         const runtime_detail::TokenSink &on_token,
                         const runtime_detail::StopPredicate &should_cancel,
                         GenerationStats *stats = nullptr);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace qtrans::core
