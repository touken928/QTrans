#pragma once

#include "qtrans/core/options.h"
#include "qtrans/core/runtime.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace qtrans::core {

class ITranslationModel {
public:
    virtual ~ITranslationModel() = default;

    virtual RuntimeKind kind() const = 0;
    virtual TranslatorOptions translator_options() const = 0;

    virtual std::string build_user_prompt(const std::string &text,
                                          const std::string &target_language) const = 0;
    virtual std::string format_chat_prompt(const std::string &user_prompt) const = 0;

    std::string format_translation_prompt(const std::string &text,
                                          const std::string &target_language) const {
        return format_chat_prompt(build_user_prompt(text, target_language));
    }

    virtual std::string format_inference_prompt(const std::string &text,
                                                const std::string &target_language) const {
        return format_translation_prompt(text, target_language);
    }
};

class ILocalTranslationModel : public ITranslationModel {
public:
    virtual const std::vector<std::uint8_t> &weights() const = 0;

    RuntimeKind kind() const final {
        return RuntimeKind::Local;
    }
};

class IRemoteTranslationModel : public ITranslationModel {
public:
    virtual const RemoteModelConfig &remote_config() const = 0;

    RuntimeKind kind() const final {
        return RuntimeKind::Remote;
    }
};

class LocalGgufModelBase : public ILocalTranslationModel {
public:
    const std::vector<std::uint8_t> &weights() const final {
        return weights_;
    }

    TranslatorOptions translator_options() const final {
        return options_;
    }

protected:
    LocalGgufModelBase(std::vector<std::uint8_t> weights, TranslatorOptions options);

    static std::vector<std::uint8_t> load_weights(const std::filesystem::path &path);

    std::vector<std::uint8_t> weights_;
    TranslatorOptions options_;
};

class RemoteApiModel final : public IRemoteTranslationModel {
public:
    using UserPromptFn = std::function<std::string(const std::string &, const std::string &)>;
    using ChatPromptFn = std::function<std::string(const std::string &)>;

    RemoteApiModel(RemoteModelConfig remote,
                   TranslatorOptions options,
                   UserPromptFn build_user_prompt,
                   ChatPromptFn format_chat_prompt = {});

    const RemoteModelConfig &remote_config() const override {
        return remote_;
    }

    TranslatorOptions translator_options() const override {
        return options_;
    }

    std::string build_user_prompt(const std::string &text,
                                  const std::string &target_language) const override;

    std::string format_chat_prompt(const std::string &user_prompt) const override;

    std::string format_inference_prompt(const std::string &text,
                                        const std::string &target_language) const override;

private:
    RemoteModelConfig remote_;
    TranslatorOptions options_;
    UserPromptFn build_user_prompt_;
    ChatPromptFn format_chat_prompt_;
};

}  // namespace qtrans::core
