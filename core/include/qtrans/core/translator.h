#pragma once

#include "qtrans/core/cancellation.h"
#include "qtrans/core/options.h"
#include "qtrans/core/runtime.h"
#include "qtrans/core/types.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace qtrans::core {

struct TranslationCallbacks {
    std::function<void(const std::string &piece)> on_token;
    std::function<void(bool is_back_channel)> on_reset;
    std::function<void(int chunk, int total)> on_chunk_begin;
};

class Translator {
public:
    explicit Translator(TranslatorOptions options = {});
    explicit Translator(std::unique_ptr<ITranslationRuntime> runtime,
                        TranslatorOptions options = {});
    ~Translator();

    Translator(const Translator &) = delete;
    Translator &operator=(const Translator &) = delete;
    Translator(Translator &&) noexcept;
    Translator &operator=(Translator &&) noexcept;

    void initialize_backend(const BackendOptions &options);
    std::string backend_label() const;

    void load_model(const std::filesystem::path &model_path);
    void load_remote_model(const RemoteModelConfig &remote);
    void unload_model();
    bool is_loaded() const;

    RuntimeKind runtime_kind() const;
    int count_prompt_tokens(const std::string &text, const std::string &target_language) const;

    TranslationResult translate(
        const TranslationRequest &request,
        const TranslationCallbacks &callbacks = {},
        const CancellationToken *cancel = nullptr);

    TranslationResult translate(
        const TranslationRequest &request,
        const TranslationCallbacks &callbacks,
        std::function<bool()> should_cancel);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace qtrans::core
