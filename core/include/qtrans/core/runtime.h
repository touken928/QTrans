#pragma once

#include "qtrans/core/options.h"
#include "qtrans/core/types.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace qtrans::core {

enum class RuntimeKind {
    Local,
    Remote,
};

struct RemoteModelConfig {
    std::string endpoint_url;
    std::string api_key;
    std::string model_name;
    std::string api_provider;
};

class ITranslationRuntime {
public:
    virtual ~ITranslationRuntime() = default;

    static void initialize_default_backend(const BackendOptions &opts);

    virtual void initialize_backend(const BackendOptions &opts) = 0;

    virtual void load_model(const std::vector<std::uint8_t> &data,
                            const TranslatorOptions &config) = 0;

    virtual void load_remote(const RemoteModelConfig &remote,
                             const TranslatorOptions &config) = 0;

    virtual void unload() = 0;
    virtual bool is_loaded() const = 0;

    virtual std::string translate(
        const std::string &prompt,
        const std::function<void(const std::string &)> &on_token = nullptr,
        const std::function<bool()> &should_cancel = nullptr) = 0;

    virtual int count_prompt_tokens(const std::string &prompt) const = 0;

    virtual std::string backend_label() const = 0;
    virtual RuntimeKind kind() const = 0;

    static std::vector<std::uint8_t> read_file(const std::filesystem::path &path);
};

}  // namespace qtrans::core
