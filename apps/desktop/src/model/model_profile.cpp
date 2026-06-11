#include "model/model_profile.h"

namespace {

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
};

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
};

const LocalModel18BProfile s_hymt18b;
const LocalModel7BProfile s_hymt7b;

}  // namespace

const ModelProfile &hymt18b_q4_profile() {
    return s_hymt18b;
}
const ModelProfile &hymt7b_q4_profile() {
    return s_hymt7b;
}
