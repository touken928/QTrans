#pragma once

#include "translation/translation_model.h"

#include <memory>
#include <string>

struct llama_model;

enum class InferenceModelKind {
    Local,
};

class ModelProfile {
public:
    virtual ~ModelProfile() = default;

    virtual const char *id() const = 0;
    virtual const char *display_name() const = 0;
    virtual const char *filename() const = 0;
    virtual const char *remote_spec() const = 0;
    virtual const char *modelscope_remote_spec() const = 0;
    virtual int download_hub() const {
        return 2;
    }
    virtual InferenceModelKind model_kind() const {
        return InferenceModelKind::Local;
    }

    virtual TranslationModelConfig default_config() const;

    virtual std::string format_prompt(
        const std::string &user_prompt,
        const struct llama_model *model) const = 0;

    virtual std::unique_ptr<TranslationModel> create_model() const;
};

const ModelProfile &hymt18b_q4_profile();
const ModelProfile &hymt7b_q4_profile();
