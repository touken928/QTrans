#pragma once

#include "translation/model_profile.h"
#include "translation/translation_model.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

class LocalModel : public TranslationModel {
public:
    explicit LocalModel(const ModelProfile &profile);
    ~LocalModel() override;

    void load(const std::vector<std::uint8_t> &data, const TranslationModelConfig &config) override;
    std::string translate(
        const std::string &text,
        const std::string &target_language,
        const std::function<void(const std::string &)> &on_token = nullptr,
        const std::function<bool()> &should_cancel = nullptr) override;

    int count_prompt_tokens(const std::string &text, const std::string &target_language) const;

    static void ensure_backend(const std::filesystem::path &plugin_dir = {});

private:
    static std::string build_user_prompt(const std::string &text, const std::string &target_language);
    static bool contains_chinese(const std::string &text);
    std::string format_chat_prompt(const std::string &user_prompt) const;
    std::string generate(
        const std::string &prompt,
        const std::function<void(const std::string &)> &on_token = nullptr,
        const std::function<bool()> &should_cancel = nullptr);

    const ModelProfile &profile_;
    TranslationModelConfig config_;
    std::unique_ptr<LlamaModelFromMemory> model_holder_;
    struct llama_context *ctx_ = nullptr;
    struct llama_sampler *sampler_ = nullptr;

    friend struct LocalModelTestAccess;
};
