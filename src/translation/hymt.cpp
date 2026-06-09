#include "translation/hymt.h"

#include "log/ai_trace.h"
#include "log/component.h"
#include "log/logger.h"
#include "model/runtime_capabilities.h"
#include "text/utf8_stream_buffer.h"
#include "translation/translation_languages.h"
#include "ggml-backend.h"
#include "llama.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

constexpr const char k_hy_bos[] = u8"<\xEF\xBD\x9Chy_begin\xe2\x96\x81of\xe2\x96\x81sentence\xEF\xBD\x9C>";
constexpr const char k_hy_user[] = u8"<\xEF\xBD\x9Chy_User\xEF\xBD\x9C>";
constexpr const char k_hy_assistant[] = u8"<\xEF\xBD\x9Chy_Assistant\xEF\xBD\x9C>";

bool is_chinese_target(const std::string &target_language) {
    return is_chinese_language_name(target_language);
}

void log_llama_text(ggml_log_level level, const char *text) {
    if (text == nullptr) {
        return;
    }
    std::string_view message(text);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
        message.remove_suffix(1);
    }
    if (message.empty()) {
        return;
    }

    auto logger = qtrans::log::get(qtrans::log::Component::Hymt);
    switch (level) {
        case GGML_LOG_LEVEL_ERROR:
            logger->error("{}", message);
            break;
#ifndef NDEBUG
        case GGML_LOG_LEVEL_WARN:
            logger->warn("{}", message);
            break;
        case GGML_LOG_LEVEL_INFO:
            logger->info("{}", message);
            break;
        case GGML_LOG_LEVEL_DEBUG:
            logger->debug("{}", message);
            break;
#endif
        case GGML_LOG_LEVEL_CONT:
        case GGML_LOG_LEVEL_NONE:
        default:
            break;
    }
}

void set_log_callback() {
    llama_log_set(
        [](ggml_log_level level, const char *text, void *) {
            log_llama_text(level, text);
        },
        nullptr);
}

// Official inference params: https://huggingface.co/tencent/Hy-MT2-1.8B-GGUF
llama_sampler *create_sampler(const TranslationModelConfig &config) {
    llama_sampler *chain = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(chain, llama_sampler_init_penalties(-1, config.repeat_penalty, 0.0f, 0.0f));
    llama_sampler_chain_add(chain, llama_sampler_init_top_k(config.top_k));
    llama_sampler_chain_add(chain, llama_sampler_init_top_p(config.top_p, 1));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(config.temperature));
    llama_sampler_chain_add(chain, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    return chain;
}

bool abort_callback(void *data) {
    if (data == nullptr) {
        return false;
    }
    const auto *should_cancel = static_cast<const std::function<bool()> *>(data);
    return (*should_cancel)();
}

std::string token_to_text(const llama_vocab *vocab, llama_token token) {
    char buffer[256];
    int piece_len = llama_token_to_piece(vocab, token, buffer, sizeof(buffer), 0, true);
    if (piece_len < 0) {
        std::vector<char> large_buffer(static_cast<size_t>(-piece_len));
        piece_len = llama_token_to_piece(vocab, token, large_buffer.data(),
                                         static_cast<int32_t>(large_buffer.size()), 0, true);
        if (piece_len < 0) {
            throw std::runtime_error("failed to convert token to text");
        }
        return std::string(large_buffer.data(), static_cast<size_t>(piece_len));
    }
    return std::string(buffer, static_cast<size_t>(piece_len));
}

}  // namespace

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

void Hymt::ensure_backend(const std::filesystem::path &plugin_dir) {
    static bool initialized = false;
    if (!initialized) {
        set_log_callback();
        llama_backend_init();
#ifdef QTRANS_MULTI_BACKEND
        RuntimeCapabilities::instance().set_plugin_dir(plugin_dir);
        if (!plugin_dir.empty()) {
            ggml_backend_load_all_from_path(plugin_dir.string().c_str());
        } else {
            ggml_backend_load_all();
        }
#else
        (void)plugin_dir;
#endif
        RuntimeCapabilities::instance().refresh();
        initialized = true;
    }
}

void Hymt::load(const std::vector<std::uint8_t> &data, const TranslationModelConfig &config) {
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
    model_params.n_gpu_layers = config.n_gpu_layers;

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

int Hymt::count_prompt_tokens(const std::string &text, const std::string &target_language) const {
    if (!is_loaded()) {
        return 0;
    }

    const std::string prompt = format_chat_prompt(build_user_prompt(text, target_language));
    const llama_vocab *vocab = llama_model_get_vocab(model_holder_->model);
    const int n_prompt = -llama_tokenize(
        vocab,
        prompt.c_str(),
        static_cast<int32_t>(prompt.size()),
        nullptr,
        0,
        true,
        true);
    return n_prompt > 0 ? n_prompt : 0;
}

std::string Hymt::translate(
    const std::string &text,
    const std::string &target_language,
    const std::function<void(const std::string &)> &on_token,
    const std::function<bool()> &should_cancel) {
    if (!is_loaded()) {
        throw std::runtime_error("model is not loaded");
    }

    if (should_cancel && should_cancel()) {
        throw TranslationCancelled();
    }

    const std::string user_prompt = build_user_prompt(text, target_language);
    return generate(format_chat_prompt(user_prompt), on_token, should_cancel);
}

std::string Hymt::build_user_prompt(const std::string &text, const std::string &target_language) {
    // https://huggingface.co/tencent/Hy-MT2-1.8B-GGUF
    if (target_language == "Auto") {
        return "Translate the following segment:\n\n" + text;
    }

    if (contains_chinese(text) && !is_chinese_target(target_language)) {
        return "将以下文本翻译为" + translation_chinese_name(target_language) +
               "，注意只需要输出翻译后的结果，不要额外解释：\n\n" +
               text;
    }

    return "Translate the following segment into " + target_language +
           ", without additional explanation.\n\n" +
           text;
}

std::string Hymt::format_chat_prompt(const std::string &user_prompt) {
    // Official chat template: https://huggingface.co/tencent/Hy-MT2-1.8B-GGUF
    return std::string(k_hy_bos) + k_hy_user + user_prompt + k_hy_assistant;
}

bool Hymt::contains_chinese(const std::string &text) {
    for (size_t i = 0; i < text.size();) {
        const unsigned char b = static_cast<unsigned char>(text[i]);
        if (b >= 0xE0 && b <= 0xEF && i + 2 < text.size()) {
            const uint32_t cp = ((b & 0x0F) << 12) |
                                ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                                (static_cast<unsigned char>(text[i + 2]) & 0x3F);
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

std::string Hymt::generate(
    const std::string &prompt,
    const std::function<void(const std::string &)> &on_token,
    const std::function<bool()> &should_cancel) {
    auto logger = qtrans::log::get(qtrans::log::Component::Hymt);

    std::function<bool()> cancel_fn = should_cancel ? should_cancel : []() { return false; };
    llama_set_abort_callback(ctx_, abort_callback, &cancel_fn);

    struct AbortGuard {
        llama_context *ctx;
        ~AbortGuard() {
            llama_set_abort_callback(ctx, nullptr, nullptr);
        }
    } guard{ctx_};

    logger->trace("generate start, clearing memory");
    logger->trace("prompt: '{}'", prompt);
    llama_memory_clear(llama_get_memory(ctx_), true);
    logger->trace("memory cleared, resetting sampler");
    llama_sampler_reset(sampler_);

    const llama_vocab *vocab = llama_model_get_vocab(model_holder_->model);
    logger->trace("tokenizing prompt (len={})", prompt.size());

    const int n_prompt = -llama_tokenize(
        vocab,
        prompt.c_str(),
        static_cast<int32_t>(prompt.size()),
        nullptr,
        0,
        true,
        true);
    logger->trace("token count: {}", n_prompt);
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

    const int ctx_room = config_.n_ctx - n_prompt;
    if (ctx_room <= 0) {
        throw std::runtime_error("prompt exceeds model context");
    }
    const int max_gen = std::min(config_.max_tokens, ctx_room);

    logger->debug(
        "starting decode loop, max_gen={} prompt_tokens={} n_ctx={}",
        max_gen,
        n_prompt,
        config_.n_ctx);
    qtrans::text::Utf8StreamBuffer utf8_stream;
    for (int i = 0; i < max_gen; ++i) {
        if (i > 0 && i % 10 == 0) {
            logger->trace("token {}/{}", i, max_gen);
        }
        if (cancel_fn()) {
            logger->debug("cancelled at token {}", i);
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
            logger->debug("EOG at token {}", i);
            break;
        }

        const std::string piece = token_to_text(vocab, token);
        response.append(piece);
        logger->trace("token {}:'{}'", i, piece);
        if (on_token) {
            const std::string emit = utf8_stream.push(piece);
            if (!emit.empty()) {
                on_token(emit);
            }
        }
        if (cancel_fn()) {
            logger->debug("cancelled at token {}", i);
            throw TranslationCancelled();
        }
        batch = llama_batch_get_one(const_cast<llama_token *>(&token), 1);
    }

    if (on_token) {
        const std::string tail = utf8_stream.flush();
        if (!tail.empty()) {
            on_token(tail);
        }
    }

    logger->trace("generate done, response_len={} response:'{}'", response.size(), response);

    qtrans::log::write_ai_trace(prompt, response);

    return response;
}
