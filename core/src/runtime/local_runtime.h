#pragma once

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
    ~LocalRuntime() override;

    LocalRuntime(const LocalRuntime &) = delete;
    LocalRuntime &operator=(const LocalRuntime &) = delete;
    LocalRuntime(LocalRuntime &&) noexcept;
    LocalRuntime &operator=(LocalRuntime &&) noexcept;

    void initialize_backend(const BackendOptions &opts) override;
    void load_model(const std::vector<std::uint8_t> &data, const TranslatorOptions &config) override;
    void load_remote(const RemoteModelConfig &remote, const TranslatorOptions &config) override;
    void unload() override;
    bool is_loaded() const override;

    void set_prompt_formatter(PromptFormatterPtr formatter) override;

    std::string translate(
        const std::string &text,
        const std::string &target_language,
        const std::function<void(const std::string &)> &on_token = nullptr,
        const std::function<bool()> &should_cancel = nullptr) override;

    int count_prompt_tokens(const std::string &text,
                            const std::string &target_language) const override;

    std::string backend_label() const override;
    RuntimeKind kind() const override;

private:
    std::string generate(
        const std::string &prompt,
        const std::function<void(const std::string &)> &on_token = nullptr,
        const std::function<bool()> &should_cancel = nullptr);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace qtrans::core
