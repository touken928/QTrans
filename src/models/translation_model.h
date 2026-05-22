#pragma once

#include "llama.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TranslationModelConfig {
    int n_ctx = 4096;
    int max_tokens = 4096;
    float temperature = 0.7f;
    int top_k = 20;
    float top_p = 0.6f;
    float repeat_penalty = 1.05f;
};

struct LlamaModelFromMemory {
    std::vector<std::uint8_t> buffer;
    FILE * file = nullptr;
    llama_model * model = nullptr;

    LlamaModelFromMemory() = default;
    ~LlamaModelFromMemory();

    LlamaModelFromMemory(const LlamaModelFromMemory &) = delete;
    LlamaModelFromMemory & operator=(const LlamaModelFromMemory &) = delete;
    LlamaModelFromMemory(LlamaModelFromMemory && other) noexcept;
    LlamaModelFromMemory & operator=(LlamaModelFromMemory && other) noexcept;
};

LlamaModelFromMemory load_llama_model_from_memory(
    const std::vector<std::uint8_t> & data,
    const llama_model_params & params);

class TranslationModel {
public:
    virtual ~TranslationModel() = default;

    TranslationModel(const TranslationModel &) = delete;
    TranslationModel & operator=(const TranslationModel &) = delete;

    virtual void load(const std::vector<std::uint8_t> & data, const TranslationModelConfig & config) = 0;
    virtual std::string translate(
        const std::string & text,
        const std::string & target_language,
        const std::function<void(const std::string &)> & on_token = nullptr) = 0;

    bool is_loaded() const { return loaded_; }

protected:
    TranslationModel() = default;

    void set_loaded(bool loaded) { loaded_ = loaded; }

private:
    bool loaded_ = false;
};
