#pragma once

#include "qtrans/core/options.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace qtrans::core {

class ITranslationPromptStrategy {
public:
    virtual ~ITranslationPromptStrategy() = default;

    virtual std::string build_user_prompt(std::string_view text,
                                          std::string_view target_language) const = 0;
    virtual std::string format_chat_prompt(std::string_view user_prompt) const = 0;

    std::string format_translation_prompt(std::string_view text,
                                          std::string_view target_language) const {
        return format_chat_prompt(build_user_prompt(text, target_language));
    }

    virtual std::string format_inference_prompt(std::string_view text,
                                                std::string_view target_language) const {
        return format_translation_prompt(text, target_language);
    }
};

class FunctionPromptStrategy final : public ITranslationPromptStrategy {
public:
    using UserPromptFn = std::function<std::string(std::string_view, std::string_view)>;
    using ChatPromptFn = std::function<std::string(std::string_view)>;
    using InferencePromptFn = std::function<std::string(std::string_view, std::string_view)>;

    FunctionPromptStrategy(UserPromptFn build_user_prompt,
                           ChatPromptFn format_chat_prompt = {},
                           InferencePromptFn format_inference_prompt = {});

    std::string build_user_prompt(std::string_view text,
                                  std::string_view target_language) const override;
    std::string format_chat_prompt(std::string_view user_prompt) const override;
    std::string format_inference_prompt(std::string_view text,
                                        std::string_view target_language) const override;

private:
    UserPromptFn build_user_prompt_;
    ChatPromptFn format_chat_prompt_;
    InferencePromptFn format_inference_prompt_;
};

struct LocalModelConfig {
    std::vector<std::uint8_t> weights;
};

struct RemoteModelConfig {
    std::string endpoint_url;
    std::string api_key;
    std::string model_name;
    std::string api_provider;
};

using ModelLoadSpec = std::variant<LocalModelConfig, RemoteModelConfig>;

struct TranslationProfile {
    ModelLoadSpec model;
    std::shared_ptr<const ITranslationPromptStrategy> prompt_strategy;
    TranslatorOptions options;

    [[nodiscard]] std::optional<std::string> validate() const noexcept;
};

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path &path);

}  // namespace qtrans::core
