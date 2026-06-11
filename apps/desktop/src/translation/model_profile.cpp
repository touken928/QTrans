#include "translation/model_profile.h"

#include "translation/local_model.h"
#include "llama.h"

#include <array>

// =============================================================================
// Hy-MT2 1.8B (Q4) — hardcoded prompt tokens
// =============================================================================

namespace {

constexpr const char k_hy_bos[] = u8"<\xEF\xBD\x9Chy_begin\xe2\x96\x81of\xe2\x96\x81sentence\xEF\xBD\x9C>";
constexpr const char k_hy_user[] = u8"<\xEF\xBD\x9Chy_User\xEF\xBD\x9C>";
constexpr const char k_hy_assistant[] = u8"<\xEF\xBD\x9Chy_Assistant\xEF\xBD\x9C>";

class LocalModel18BProfile : public ModelProfile {
public:
    const char *id() const override {
        return "hymt2-q4";
    }
    const char *display_name() const override {
        return "Hy-MT2-1.8B (Q4)";
    }
    const char *filename() const override {
        return "Hy-MT2-1.8B-Q4_K_M.gguf";
    }
    const char *remote_spec() const override {
        return "tencent/Hy-MT2-1.8B-GGUF/Hy-MT2-1.8B-Q4_K_M.gguf";
    }
    const char *modelscope_remote_spec() const override {
        return "Tencent-Hunyuan/Hy-MT2-1.8B-GGUF/Hy-MT2-1.8B-Q4_K_M.gguf";
    }

    std::string format_prompt(
        const std::string &user_prompt,
        const struct llama_model *model) const override {
        (void)model;
        return std::string(k_hy_bos) + k_hy_user + user_prompt + k_hy_assistant;
    }
};

// =============================================================================
// Hy-MT2 7B (Q4) — GGUF chat template
// =============================================================================

class LocalModel7BProfile : public ModelProfile {
public:
    const char *id() const override {
        return "hymt2-7b-q4";
    }
    const char *display_name() const override {
        return "Hy-MT2-7B (Q4)";
    }
    const char *filename() const override {
        return "Hy-MT2-7B-Q4_K_M.gguf";
    }
    const char *remote_spec() const override {
        return "tencent/Hy-MT2-7B-GGUF/Hy-MT2-7B-Q4_K_M.gguf";
    }
    const char *modelscope_remote_spec() const override {
        return "Tencent-Hunyuan/Hy-MT2-7B-GGUF/Hy-MT2-7B-Q4_K_M.gguf";
    }

    std::string format_prompt(
        const std::string &user_prompt,
        const struct llama_model *model) const override {
        if (model == nullptr) {
            return user_prompt;
        }
        std::array<char, 8192> tmpl_buf{};
        const int tmpl_len = llama_model_meta_val_str(
            model, "tokenizer.chat_template", tmpl_buf.data(), tmpl_buf.size());
        if (tmpl_len > 0) {
            const std::string tmpl(tmpl_buf.data(), static_cast<size_t>(tmpl_len));
            const llama_chat_message chat[] = {
                {"user", user_prompt.c_str()},
            };
            std::array<char, 16384> buf{};
            const int len = llama_chat_apply_template(
                tmpl.c_str(), chat, 1, true, buf.data(), static_cast<int32_t>(buf.size()) - 1);
            if (len > 0) {
                return std::string(buf.data(), static_cast<size_t>(len));
            }
        }
        return user_prompt;
    }
};

// =============================================================================
// Registry
// =============================================================================

const LocalModel18BProfile s_hymt18b;
const LocalModel7BProfile s_hymt7b;

}  // namespace

const ModelProfile &hymt18b_q4_profile() {
    return s_hymt18b;
}
const ModelProfile &hymt7b_q4_profile() {
    return s_hymt7b;
}

// =============================================================================
// ModelProfile default implementations
// =============================================================================

TranslationModelConfig ModelProfile::default_config() const {
    TranslationModelConfig cfg{};
    cfg.n_ctx = 4096;
    cfg.max_tokens = 4096;
    cfg.n_gpu_layers = -1;
    return cfg;
}

std::unique_ptr<TranslationModel> ModelProfile::create_model() const {
    return std::make_unique<LocalModel>(*this);
}
