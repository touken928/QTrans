#pragma once

#include "qtrans/core/options.h"
#include "qtrans/core/prompt_formatter.h"
#include "qtrans/core/runtime.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace qtrans::core {

class ITranslationModel {
public:
    virtual ~ITranslationModel() = default;

    virtual RuntimeKind kind() const = 0;
    virtual TranslatorOptions translator_options() const = 0;
    virtual PromptFormatterPtr prompt_formatter() const = 0;
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
    RemoteApiModel(RemoteModelConfig remote,
                   TranslatorOptions options,
                   PromptFormatterPtr formatter);

    const RemoteModelConfig &remote_config() const override {
        return remote_;
    }

    TranslatorOptions translator_options() const override {
        return options_;
    }

    PromptFormatterPtr prompt_formatter() const override;

private:
    RemoteModelConfig remote_;
    TranslatorOptions options_;
    PromptFormatterPtr formatter_;
};

}  // namespace qtrans::core
