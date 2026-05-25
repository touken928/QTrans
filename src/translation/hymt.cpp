#include "translation/hymt.h"

#include "translation/translation_languages.h"
#include "llama.h"

#include <cstdio>
#include <ctime>
#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

constexpr const char k_hy_bos[] = u8"<\xEF\xBD\x9Chy_begin\xe2\x96\x81of\xe2\x96\x81sentence\xEF\xBD\x9C>";
constexpr const char k_hy_user[] = u8"<\xEF\xBD\x9Chy_User\xEF\xBD\x9C>";
constexpr const char k_hy_assistant[] = u8"<\xEF\xBD\x9Chy_Assistant\xEF\xBD\x9C>";

bool is_chinese_target(const std::string & target_language) {
    return is_chinese_language_name(target_language);
}

bool contains_chinese(const std::string & text) {
    for (size_t i = 0; i < text.size(); ) {
        const unsigned char b = static_cast<unsigned char>(text[i]);
        if (b >= 0xE0 && b <= 0xEF && i + 2 < text.size()) {
            const uint32_t cp = ((b & 0x0F) << 12)
                | ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6)
                | (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            if (cp >= 0x4E00 && cp <= 0x9FFF) {
                return true;
            }
            i += 3;
        } else {
            ++i;
        }
    }
    return false;
}

void set_log_callback() {
    llama_log_set([](ggml_log_level level, const char * text, void *) {
        if (level >= GGML_LOG_LEVEL_ERROR) {
            std::fputs(text, stderr);
        }
    }, nullptr);
}

// Official inference params: https://huggingface.co/tencent/Hy-MT2-1.8B-GGUF
llama_sampler * create_sampler(const TranslationModelConfig & config) {
    llama_sampler * chain = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(chain, llama_sampler_init_penalties(-1, config.repeat_penalty, 0.0f, 0.0f));
    llama_sampler_chain_add(chain, llama_sampler_init_top_k(config.top_k));
    llama_sampler_chain_add(chain, llama_sampler_init_top_p(config.top_p, 1));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(config.temperature));
    llama_sampler_chain_add(chain, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    return chain;
}

bool abort_callback(void * data) {
    if (data == nullptr) {
        return false;
    }
    const auto * should_cancel = static_cast<const std::function<bool()> *>(data);
    return (*should_cancel)();
}

} // namespace

Hymt::~Hymt() {
    if (sampler_ != nullptr) {
        llama_sampler_free(sampler_);
        sampler_ = nullptr;
    }
    if (ctx_ != nullptr) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    model_holder_.reset();
    set_loaded(false);
}

void Hymt::ensure_backend() {
    static bool initialized = false;
    if (!initialized) {
        set_log_callback();
        llama_backend_init();
        ggml_backend_load_all();
        initialized = true;
    }
}

void Hymt::load(const std::vector<std::uint8_t> & data, const TranslationModelConfig & config) {
    if (data.empty()) {
        throw std::invalid_argument("model data is empty");
    }

    ensure_backend();
    config_ = config;

    if (sampler_ != nullptr) {
        llama_sampler_free(sampler_);
        sampler_ = nullptr;
    }
    if (ctx_ != nullptr) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    model_holder_.reset();

    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = 0;

    model_holder_ = std::make_unique<LlamaModelFromMemory>(
        load_llama_model_from_memory(data, model_params));

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = static_cast<uint32_t>(config.n_ctx);
    ctx_params.n_batch = static_cast<uint32_t>(config.n_ctx);

    ctx_ = llama_init_from_model(model_holder_->model, ctx_params);
    if (ctx_ == nullptr) {
        model_holder_.reset();
        throw std::runtime_error("failed to create llama context");
    }

    sampler_ = create_sampler(config_);
    set_loaded(true);
}

std::string Hymt::translate(
    const std::string & text,
    const std::string & target_language,
    const std::function<void(const std::string &)> & on_token,
    const std::function<bool()> & should_cancel) {
    if (!is_loaded()) {
        throw std::runtime_error("model is not loaded");
    }

    if (should_cancel && should_cancel()) {
        throw TranslationCancelled();
    }

    const std::string user_prompt = build_user_prompt(text, target_language);
    return generate(format_chat_prompt(user_prompt), on_token, should_cancel);
}

std::string Hymt::build_user_prompt(const std::string & text, const std::string & target_language) {
    // ZH=>XX: Chinese instruction template with Chinese target language names.
    // XX=>XX (including XX=>ZH): English template with English target language names.
    // https://huggingface.co/tencent/Hy-MT2-1.8B-GGUF
    if (contains_chinese(text) && !is_chinese_target(target_language)) {
        return "将以下文本翻译为" + translation_chinese_name(target_language) +
               "，注意只需要输出翻译后的结果，不要额外解释：\n\n" +
               text;
    }

    return "Translate the following segment into " + target_language +
           ", without additional explanation.\n\n" +
           text;
}

std::string Hymt::format_chat_prompt(const std::string & user_prompt) {
    // Official chat template: https://huggingface.co/tencent/Hy-MT2-1.8B-GGUF
    return std::string(k_hy_bos) + k_hy_user + user_prompt + k_hy_assistant;
}

std::string Hymt::generate(
    const std::string & prompt,
    const std::function<void(const std::string &)> & on_token,
    const std::function<bool()> & should_cancel) {
    std::function<bool()> cancel_fn = should_cancel ? should_cancel : []() { return false; };
    llama_set_abort_callback(ctx_, abort_callback, &cancel_fn);

    struct AbortGuard {
        llama_context* ctx;
        ~AbortGuard() { llama_set_abort_callback(ctx, nullptr, nullptr); }
    } guard{ctx_};

    fprintf(stderr, "[Hymt] generate start, clearing memory\n");
    fprintf(stderr, "[Hymt] prompt: '%s'\n", prompt.c_str());
    llama_memory_clear(llama_get_memory(ctx_), true);
    fprintf(stderr, "[Hymt] memory cleared, resetting sampler\n");
    llama_sampler_reset(sampler_);

    const llama_vocab * vocab = llama_model_get_vocab(model_holder_->model);
    fprintf(stderr, "[Hymt] vocab obtained, tokenizing prompt (len=%d)\n",
            static_cast<int>(prompt.size()));

    const int n_prompt = -llama_tokenize(
        vocab,
        prompt.c_str(),
        static_cast<int32_t>(prompt.size()),
        nullptr,
        0,
        true,
        true);
    fprintf(stderr, "[Hymt] token count: %d\n", n_prompt);
    if (n_prompt <= 0) {
        throw std::runtime_error("failed to measure prompt tokens");
    }

    std::vector<llama_token> prompt_tokens(static_cast<size_t>(n_prompt));
    if (llama_tokenize(
            vocab,
            prompt.c_str(),
            static_cast<int32_t>(prompt.size()),
            prompt_tokens.data(),
            static_cast<int32_t>(prompt_tokens.size()),
            true,
            true) < 0) {
        throw std::runtime_error("failed to tokenize prompt");
    }

    llama_batch batch = llama_batch_get_one(prompt_tokens.data(), static_cast<int32_t>(prompt_tokens.size()));
    std::string response;

    fprintf(stderr, "[Hymt] starting decode loop, max_tokens=%d\n", config_.max_tokens);
    for (int i = 0; i < config_.max_tokens; ++i) {
        if (i > 0 && i % 10 == 0) {
            fprintf(stderr, "[Hymt] token %d/%d\n", i, config_.max_tokens);
        }
        if (cancel_fn()) {
            fprintf(stderr, "[Hymt] cancelled at token %d\n", i);
            throw TranslationCancelled();
        }

        const int decode_status = llama_decode(ctx_, batch);
        if (decode_status != 0) {
            if (cancel_fn()) {
                throw TranslationCancelled();
            }
            throw std::runtime_error("llama_decode failed");
        }

        const llama_token token = llama_sampler_sample(sampler_, ctx_, -1);
        if (llama_vocab_is_eog(vocab, token)) {
            fprintf(stderr, "[Hymt] EOG at token %d\n", i);
            break;
        }

        char piece[256];
        const int piece_len = llama_token_to_piece(vocab, token, piece, sizeof(piece), 0, true);
        if (piece_len < 0) {
            throw std::runtime_error("failed to convert token to text");
        }

        response.append(piece, static_cast<size_t>(piece_len));
        fprintf(stderr, "[Hymt] token %d:'%s'\n", i, std::string(piece, static_cast<size_t>(piece_len)).c_str());
        if (on_token) {
            on_token(std::string(piece, static_cast<size_t>(piece_len)));
        }
        if (cancel_fn()) {
            fprintf(stderr, "[Hymt] cancelled at token %d\n", i);
            throw TranslationCancelled();
        }
        batch = llama_batch_get_one(const_cast<llama_token *>(&token), 1);
    }

    fprintf(stderr, "[Hymt] generate done, response_len=%zu response:'%s'\n",
            response.size(), response.c_str());

    {
        time_t now = time(nullptr);
        char time_buf[32];
        strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S", localtime(&now));
        char log_path[512];
        snprintf(log_path, sizeof(log_path), "%s/qtrans_ai_output_%s.log",
                 getenv("TEMP") ? getenv("TEMP") : ".", time_buf);
        FILE * logf = fopen(log_path, "wb");
        if (logf) {
            fprintf(logf, "=== prompt ===\n%s\n\n=== response ===\n%s\n",
                    prompt.c_str(), response.c_str());
            fclose(logf);
            fprintf(stderr, "[Hymt] log written to: %s\n", log_path);
        }
    }

    return response;
}
