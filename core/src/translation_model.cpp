#include "qtrans/core/translation_model.h"

#include <stdexcept>
#include <utility>

namespace qtrans::core {

namespace {

PromptFormatterPtr require_formatter(const PromptFormatterPtr &formatter) {
    if (formatter == nullptr || !formatter->is_configured()) {
        throw std::runtime_error("prompt formatter is not configured");
    }
    return formatter;
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
                               PromptFormatterPtr formatter)
    : remote_(std::move(remote)),
      options_(options),
      formatter_(std::move(formatter)) {
    require_formatter(formatter_);
}

PromptFormatterPtr RemoteApiModel::prompt_formatter() const {
    return require_formatter(formatter_);
}

}  // namespace qtrans::core
