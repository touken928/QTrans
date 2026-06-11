#pragma once

#include "qtrans/core/runtime.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace qtrans::core {

class RemoteRuntime : public ITranslationRuntime {
public:
    RemoteRuntime();
    ~RemoteRuntime() override;

    RemoteRuntime(const RemoteRuntime &) = delete;
    RemoteRuntime &operator=(const RemoteRuntime &) = delete;
    RemoteRuntime(RemoteRuntime &&) noexcept;
    RemoteRuntime &operator=(RemoteRuntime &&) noexcept;

    void initialize_backend(const BackendOptions &opts) override;
    void load_model(const std::vector<std::uint8_t> &data, const TranslatorOptions &config) override;
    void load_remote(const RemoteModelConfig &remote, const TranslatorOptions &config) override;
    void unload() override;
    bool is_loaded() const override;

    std::string translate(
        const std::string &prompt,
        const std::function<void(const std::string &)> &on_token = nullptr,
        const std::function<bool()> &should_cancel = nullptr) override;

    int count_prompt_tokens(const std::string &prompt) const override;

    std::string backend_label() const override;
    RuntimeKind kind() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace qtrans::core
