#pragma once

#include "qtrans/core/backend_environment.h"
#include "qtrans/core/runtime.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace qtrans::core {

class LocalRuntime : public ITranslationRuntime {
public:
    LocalRuntime();
    LocalRuntime(ResolvedBackendEnvironment environment, BackendOptions options);
    ~LocalRuntime() override;

    LocalRuntime(const LocalRuntime &) = delete;
    LocalRuntime &operator=(const LocalRuntime &) = delete;
    LocalRuntime(LocalRuntime &&) noexcept;
    LocalRuntime &operator=(LocalRuntime &&) noexcept;

    void load(const ModelLoadSpec &model, const TranslatorOptions &config) override;
    void unload() override;
    bool is_loaded() const override;

    std::string translate(
        const std::string &prompt,
        const std::function<void(const std::string &)> &on_token = nullptr,
        const std::function<bool()> &should_cancel = nullptr) override;

    int count_prompt_tokens(const std::string &prompt) const override;

    std::string backend_label() const override;
    RuntimeKind kind() const override;
    RuntimeTraits traits() const override;

private:
    std::string generate(
        const std::string &prompt,
        const std::function<void(const std::string &)> &on_token = nullptr,
        const std::function<bool()> &should_cancel = nullptr);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace qtrans::core
