#pragma once

#include "models/translation_model.h"

#include <functional>
#include <memory>
#include <string>

class Hymt15 : public TranslationModel {
public:
    ~Hymt15() override;

    void load(const std::vector<std::uint8_t> & data, const TranslationModelConfig & config) override;
    std::string translate(
        const std::string & text,
        const std::string & target_language,
        const std::function<void(const std::string &)> & on_token = nullptr) override;

private:
    static std::string build_user_prompt(const std::string & text, const std::string & target_language);
    static std::string format_chat_prompt(const std::string & user_prompt);

    static void ensure_backend();
    std::string generate(
        const std::string & prompt,
        const std::function<void(const std::string &)> & on_token = nullptr);

    TranslationModelConfig config_;
    std::unique_ptr<LlamaModelFromMemory> model_holder_;
    struct llama_context * ctx_ = nullptr;
    struct llama_sampler * sampler_ = nullptr;
};
