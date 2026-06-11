#include "qtrans/core/translation_model.h"

#include <stdexcept>
#include <utility>

namespace qtrans::core {

namespace {

RemoteApiModel::UserPromptFn require_user_prompt_fn(const RemoteApiModel::UserPromptFn &fn) {
    if (!fn) {
        throw std::invalid_argument("build_user_prompt is required");
    }
    return fn;
}

}  // namespace

LocalGgufModelBase::LocalGgufModelBase(std::vector<std::uint8_t> weights,
                                       TranslatorOptions options)
    : weights_(std::move(weights)),
      options_(options) {
}

std::vector<std::uint8_t> LocalGgufModelBase::load_weights(const std::filesystem::path &path) {
    return ITranslationRuntime::read_file(path);
}

RemoteApiModel::RemoteApiModel(RemoteModelConfig remote,
                               TranslatorOptions options,
                               UserPromptFn build_user_prompt,
                               ChatPromptFn format_chat_prompt)
    : remote_(std::move(remote)),
      options_(options),
      build_user_prompt_(require_user_prompt_fn(build_user_prompt)),
      format_chat_prompt_(std::move(format_chat_prompt)) {
}

std::string RemoteApiModel::build_user_prompt(const std::string &text,
                                              const std::string &target_language) const {
    return build_user_prompt_(text, target_language);
}

std::string RemoteApiModel::format_chat_prompt(const std::string &user_prompt) const {
    if (format_chat_prompt_) {
        return format_chat_prompt_(user_prompt);
    }
    return user_prompt;
}

std::string RemoteApiModel::format_inference_prompt(const std::string &text,
                                                    const std::string &target_language) const {
    return build_user_prompt(text, target_language);
}

}  // namespace qtrans::core
