#pragma once

#include "qtrans/core/options.h"
#include "qtrans/core/translation_model.h"
#include "qtrans/core/types.h"

#include <functional>
#include <memory>
#include <string>

namespace qtrans::core {

enum class RuntimeKind {
    Local,
    Remote,
};

enum class ContextHandling {
    LocalEnforced,
    RuntimeManaged,
};

enum class StreamingSupport {
    TokenByToken,
    FullResultCallback,
};

struct RuntimeTraits {
    RuntimeKind kind = RuntimeKind::Local;
    ContextHandling context_handling = ContextHandling::LocalEnforced;
    StreamingSupport streaming = StreamingSupport::TokenByToken;
    bool has_precise_token_counting = false;
    int max_input_tokens = 0;
    int max_output_tokens = 0;
};

class ITranslationRuntime {
public:
    virtual ~ITranslationRuntime() = default;

    virtual void load(const ModelLoadSpec &model, const TranslatorOptions &config) = 0;

    virtual void unload() = 0;
    virtual bool is_loaded() const = 0;

    virtual std::string translate(
        const std::string &prompt,
        const std::function<void(const std::string &)> &on_token = nullptr,
        const std::function<bool()> &should_cancel = nullptr) = 0;

    virtual int count_prompt_tokens(const std::string &prompt) const = 0;

    virtual std::string backend_label() const = 0;
    virtual RuntimeKind kind() const = 0;
    virtual RuntimeTraits traits() const = 0;
};

class ITranslationRuntimeFactory {
public:
    virtual ~ITranslationRuntimeFactory() = default;
    virtual std::unique_ptr<ITranslationRuntime> create_runtime(const ModelLoadSpec &model) = 0;
};

}  // namespace qtrans::core
