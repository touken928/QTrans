#include "qtrans/core/translation_model.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <utility>

namespace qtrans::core {

namespace {

FunctionPromptStrategy::UserPromptFn require_user_prompt_fn(
    const FunctionPromptStrategy::UserPromptFn &fn) {
    if (!fn) {
        throw std::invalid_argument("build_user_prompt is required");
    }
    return fn;
}

}  // namespace

FunctionPromptStrategy::FunctionPromptStrategy(UserPromptFn build_user_prompt,
                                               ChatPromptFn format_chat_prompt,
                                               InferencePromptFn format_inference_prompt)
    : build_user_prompt_(require_user_prompt_fn(build_user_prompt)),
      format_chat_prompt_(std::move(format_chat_prompt)),
      format_inference_prompt_(std::move(format_inference_prompt)) {
}

std::string FunctionPromptStrategy::build_user_prompt(std::string_view text,
                                                      std::string_view target_language) const {
    return build_user_prompt_(text, target_language);
}

std::string FunctionPromptStrategy::format_chat_prompt(std::string_view user_prompt) const {
    if (format_chat_prompt_) {
        return format_chat_prompt_(user_prompt);
    }
    return std::string(user_prompt);
}

std::string FunctionPromptStrategy::format_inference_prompt(std::string_view text,
                                                            std::string_view target_language) const {
    if (format_inference_prompt_) {
        return format_inference_prompt_(text, target_language);
    }
    return format_translation_prompt(text, target_language);
}

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open model file: " + path.string());
    }

    input.seekg(0, std::ios::end);
    const std::streamsize size = input.tellg();
    if (size < 0) {
        throw std::runtime_error("failed to determine file size: " + path.string());
    }

    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<size_t>(size));
    if (size > 0) {
        input.read(reinterpret_cast<char *>(data.data()), size);
        if (!input) {
            throw std::runtime_error("failed to read model file: " + path.string());
        }
    }
    return data;
}

std::optional<std::string> TranslationProfile::validate() const noexcept {
    if (!prompt_strategy) {
        return "translation prompt strategy is required";
    }
    if (options.context.n_ctx <= 0) {
        return "context window (n_ctx) must be positive";
    }
    if (options.context.max_tokens <= 0) {
        return "max output tokens must be positive";
    }
    if (const auto *local = std::get_if<LocalModelConfig>(&model)) {
        if (local->path.empty() && local->weights.empty()) {
            return "local model source is empty";
        }
    } else if (const auto *remote = std::get_if<RemoteModelConfig>(&model)) {
        if (remote->endpoint_url.empty()) {
            return "remote model endpoint URL is empty";
        }
        if (remote->model_name.empty()) {
            return "remote model name is empty";
        }
    } else {
        return "model load spec is empty";
    }
    return std::nullopt;
}

}  // namespace qtrans::core
