#include "local_runtime.h"

#include "text/language_names.h"
#include "text/utf8_stream_buffer.h"

#include "ggml-backend.h"
#include "llama.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace qtrans::core {

namespace {

// Hy-MT prompt template constants.
constexpr const char k_hy_bos[] = u8"<\xEF\xBD\x9Chy_begin\xe2\x96\x81of\xe2\x96\x81sentence\xEF\xBD\x9C>";
constexpr const char k_hy_user[] = u8"<\xEF\xBD\x9Chy_User\xEF\xBD\x9C>";
constexpr const char k_hy_assistant[] = u8"<\xEF\xBD\x9Chy_Assistant\xEF\xBD\x9C>";

// ---------------------------------------------------------------------------
// Model loading — moved from translation_model.cpp, keeping llama entirely
// within this translation unit.
// ---------------------------------------------------------------------------

struct LlamaModelHolder {
    std::vector<std::uint8_t> buffer;
    FILE *file = nullptr;
    llama_model *model = nullptr;

    LlamaModelHolder() = default;
    ~LlamaModelHolder() {
        if (model != nullptr) {
            llama_model_free(model);
            model = nullptr;
        }
        if (file != nullptr) {
            std::fclose(file);
            file = nullptr;
        }
    }
    LlamaModelHolder(const LlamaModelHolder &) = delete;
    LlamaModelHolder &operator=(const LlamaModelHolder &) = delete;
    LlamaModelHolder(LlamaModelHolder &&other) noexcept
        : buffer(std::move(other.buffer)),
          file(other.file),
          model(other.model) {
        other.file = nullptr;
        other.model = nullptr;
    }
    LlamaModelHolder &operator=(LlamaModelHolder &&other) noexcept {
        if (this != &other) {
            this->~LlamaModelHolder();
            buffer = std::move(other.buffer);
            file = other.file;
            model = other.model;
            other.file = nullptr;
            other.model = nullptr;
        }
        return *this;
    }
};

FILE *open_memory_as_file(std::vector<std::uint8_t> &buffer) {
#if defined(_WIN32)
    FILE *file = std::tmpfile();
    if (file == nullptr) return nullptr;
    const size_t written = std::fwrite(buffer.data(), 1, buffer.size(), file);
    if (written != buffer.size()) { std::fclose(file); return nullptr; }
    std::rewind(file);
    return file;
#else
    return fmemopen(buffer.data(), buffer.size(), "rb");
#endif
}

LlamaModelHolder load_llama_model(const std::vector<std::uint8_t> &data,
                                   const llama_model_params &params) {
    if (data.empty()) throw std::invalid_argument("gguf data is empty");
    LlamaModelHolder holder;
    holder.buffer = data;
    holder.file = open_memory_as_file(holder.buffer);
    if (holder.file == nullptr) throw std::runtime_error("failed to open gguf memory buffer");
    llama_model_params model_params = params;
    model_params.use_mmap = false;
    holder.model = llama_model_load_from_file_ptr(holder.file, model_params);
    if (holder.model == nullptr) throw std::runtime_error("failed to load llama model from memory");
    return holder;
}

// ---------------------------------------------------------------------------
// Backend probing — moved from runtime_capabilities.cpp
// ---------------------------------------------------------------------------

bool probe_vulkan_gpu() {
#ifdef QTRANS_MULTI_BACKEND
    const ggml_backend_reg_t reg = ggml_backend_reg_by_name("VULKAN");
    if (reg == nullptr) return false;
    const size_t device_count = ggml_backend_reg_dev_count(reg);
    for (size_t i = 0; i < device_count; ++i) {
        const ggml_backend_dev_t device = ggml_backend_reg_dev_get(reg, i);
        if (ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_GPU) return true;
    }
    const ggml_backend_dev_t gpu = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU);
    return gpu != nullptr;
#else
    return false;
#endif
}

bool probe_metal_gpu() {
#ifdef QTRANS_GPU_METAL
    const ggml_backend_reg_t reg = ggml_backend_reg_by_name("MTL");
    if (reg == nullptr) return false;
    const size_t device_count = ggml_backend_reg_dev_count(reg);
    for (size_t i = 0; i < device_count; ++i) {
        const ggml_backend_dev_t device = ggml_backend_reg_dev_get(reg, i);
        if (ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_GPU) return true;
    }
    const ggml_backend_dev_t gpu = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU);
    return gpu != nullptr;
#else
    return false;
#endif
}

// ---------------------------------------------------------------------------
// Log redirect from llama
// ---------------------------------------------------------------------------

void log_llama_text(ggml_log_level level, const char *text) {
    if (text == nullptr) return;
    std::string_view message(text);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
        message.remove_suffix(1);
    if (message.empty()) return;

    auto logger = spdlog::get("hymt");
    if (!logger) return;
    switch (level) {
        case GGML_LOG_LEVEL_ERROR: logger->error("{}", message); break;
#ifndef NDEBUG
        case GGML_LOG_LEVEL_WARN:  logger->warn("{}", message); break;
        case GGML_LOG_LEVEL_INFO:  logger->info("{}", message); break;
        case GGML_LOG_LEVEL_DEBUG: logger->debug("{}", message); break;
#endif
        default: break;
    }
}

void set_llama_log_callback() {
    llama_log_set([](ggml_log_level level, const char *text, void *) {
        log_llama_text(level, text);
    }, nullptr);
}

// ---------------------------------------------------------------------------
// Sampler creation
// ---------------------------------------------------------------------------

llama_sampler *create_sampler(const TranslatorOptions &config) {
    llama_sampler *chain = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(chain, llama_sampler_init_penalties(-1, config.repeat_penalty, 0.0f, 0.0f));
    llama_sampler_chain_add(chain, llama_sampler_init_top_k(config.top_k));
    llama_sampler_chain_add(chain, llama_sampler_init_top_p(config.top_p, 1));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(config.temperature));
    llama_sampler_chain_add(chain, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    return chain;
}

// ---------------------------------------------------------------------------
// Token helpers
// ---------------------------------------------------------------------------

bool abort_callback(void *data) {
    if (data == nullptr) return false;
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
        if (piece_len < 0)
            throw std::runtime_error("failed to convert token to text");
        return std::string(large_buffer.data(), static_cast<size_t>(piece_len));
    }
    return std::string(buffer, static_cast<size_t>(piece_len));
}

// ---------------------------------------------------------------------------
// Prompt building (Hy-MT specific)
// ---------------------------------------------------------------------------

bool contains_chinese(const std::string &text) {
    for (size_t i = 0; i < text.size();) {
        const unsigned char b = static_cast<unsigned char>(text[i]);
        if (b >= 0xE0 && b <= 0xEF && i + 2 < text.size()) {
            const uint32_t cp = ((b & 0x0F) << 12) |
                                ((static_cast<unsigned char>(text[i + 1]) & 0x3F) << 6) |
                                (static_cast<unsigned char>(text[i + 2]) & 0x3F);
            if (cp >= 0x4E00 && cp <= 0x9FFF) return true;
            i += 3;
        } else {
            ++i;
        }
    }
    return false;
}

std::string build_user_prompt(const std::string &text, const std::string &target_language) {
    if (target_language == "Auto")
        return "Translate the following segment:\n\n" + text;

    if (contains_chinese(text) && !is_chinese_language_name(target_language))
        return "将以下文本翻译为" + translation_chinese_name(target_language) +
               "，注意只需要输出翻译后的结果，不要额外解释：\n\n" + text;

    return "Translate the following segment into " + target_language +
           ", without additional explanation.\n\n" + text;
}

std::string format_chat_prompt(const std::string &user_prompt) {
    return std::string(k_hy_bos) + k_hy_user + user_prompt + k_hy_assistant;
}

// ---------------------------------------------------------------------------
// AI trace helper
// ---------------------------------------------------------------------------

void write_ai_trace(const std::string &prompt, const std::string &response) {
#ifndef NDEBUG
    static std::mutex g_trace_mutex;
    static std::filesystem::path g_logs_dir;

    std::lock_guard<std::mutex> lock(g_trace_mutex);
    if (g_logs_dir.empty()) {
        const char *env = std::getenv("QTRANS_LOGS_DIR");
        if (env != nullptr && env[0] != '\0')
            g_logs_dir = env;
    }
    if (g_logs_dir.empty()) return;

    std::error_code ec;
    std::filesystem::create_directories(g_logs_dir, ec);
    if (ec) return;

    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &now_time);
#else
    localtime_r(&now_time, &tm_buf);
#endif
    std::ostringstream time_stream;
    time_stream << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
    const std::filesystem::path log_path = g_logs_dir / ("ai_output_" + time_stream.str() + ".log");

    std::ofstream logf(log_path, std::ios::binary);
    if (!logf.is_open()) return;
    logf << "=== prompt ===\n" << prompt << "\n\n=== response ===\n" << response << '\n';
#else
    (void)prompt;
    (void)response;
#endif
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// LocalRuntime::Impl — holds all llama state
// ---------------------------------------------------------------------------

struct LocalRuntime::Impl {
    LlamaModelHolder model_holder_;
    llama_context *ctx_ = nullptr;
    llama_sampler *sampler_ = nullptr;
    TranslatorOptions config_;
    bool loaded_ = false;
    // Probing results
    static std::string &s_backend_label() {
        static std::string label = "CPU";
        return label;
    }

    ~Impl() {
        if (sampler_ != nullptr) { llama_sampler_free(sampler_); sampler_ = nullptr; }
        if (ctx_ != nullptr) { llama_free(ctx_); ctx_ = nullptr; }
        model_holder_ = LlamaModelHolder();
        loaded_ = false;
    }
};

// ---------------------------------------------------------------------------
// LocalRuntime implementation
// ---------------------------------------------------------------------------

LocalRuntime::LocalRuntime() : impl_(std::make_unique<Impl>()) {}
LocalRuntime::~LocalRuntime() = default;

LocalRuntime::LocalRuntime(LocalRuntime &&other) noexcept : impl_(std::move(other.impl_)) {}
LocalRuntime &LocalRuntime::operator=(LocalRuntime &&other) noexcept {
    if (this != &other) impl_ = std::move(other.impl_);
    return *this;
}

void ITranslationRuntime::initialize_default_backend(const BackendOptions &opts) {
    LocalRuntime temp;
    temp.initialize_backend(opts);
}

void LocalRuntime::initialize_backend(const BackendOptions &opts) {
    static std::once_flag flag;
    std::call_once(flag, [&opts]() {
        set_llama_log_callback();
        llama_backend_init();
#ifdef QTRANS_MULTI_BACKEND
        ggml_backend_load_all();
        if (!opts.plugin_dir.empty())
            ggml_backend_load_all_from_path(opts.plugin_dir.string().c_str());
#endif
        const bool has_vulkan = probe_vulkan_gpu();
        const bool has_metal = probe_metal_gpu();
        if (has_vulkan)
            Impl::s_backend_label() = "Vulkan";
        else if (has_metal)
            Impl::s_backend_label() = "Metal";
        else
            Impl::s_backend_label() = "CPU";
    });
}

void LocalRuntime::load_model(const std::vector<std::uint8_t> &data, const TranslatorOptions &config) {
    if (data.empty()) throw std::invalid_argument("model data is empty");

    impl_->config_ = config;

    // Clean up previous state.
    if (impl_->sampler_ != nullptr) { llama_sampler_free(impl_->sampler_); impl_->sampler_ = nullptr; }
    if (impl_->ctx_ != nullptr) { llama_free(impl_->ctx_); impl_->ctx_ = nullptr; }
    impl_->model_holder_ = LlamaModelHolder();

    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = config.n_gpu_layers;

    impl_->model_holder_ = load_llama_model(data, model_params);

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = static_cast<uint32_t>(config.n_ctx);
    ctx_params.n_batch = static_cast<uint32_t>(config.n_ctx);

    impl_->ctx_ = llama_init_from_model(impl_->model_holder_.model, ctx_params);
    if (impl_->ctx_ == nullptr) {
        impl_->model_holder_ = LlamaModelHolder();
        throw std::runtime_error("failed to create llama context");
    }

    impl_->sampler_ = create_sampler(config);
    impl_->loaded_ = true;
}

void LocalRuntime::unload() {
    impl_->~Impl();
    new (impl_.get()) Impl();
}

bool LocalRuntime::is_loaded() const {
    return impl_->loaded_;
}

void LocalRuntime::load_remote(const RemoteModelConfig & /*remote*/,
                               const TranslatorOptions & /*config*/) {
    throw std::runtime_error("local runtime does not support remote models");
}

RuntimeKind LocalRuntime::kind() const {
    return RuntimeKind::Local;
}

int LocalRuntime::count_prompt_tokens(const std::string &text, const std::string &target_language) const {
    if (!is_loaded()) return 0;
    const std::string prompt = format_chat_prompt(build_user_prompt(text, target_language));
    const llama_vocab *vocab = llama_model_get_vocab(impl_->model_holder_.model);
    const int n_prompt = -llama_tokenize(
        vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
        nullptr, 0, true, true);
    return n_prompt > 0 ? n_prompt : 0;
}

std::string LocalRuntime::translate(
    const std::string &text,
    const std::string &target_language,
    const std::function<void(const std::string &)> &on_token,
    const std::function<bool()> &should_cancel) {
    if (!is_loaded()) throw std::runtime_error("model is not loaded");
    if (should_cancel && should_cancel()) throw std::runtime_error("translation cancelled");
    const std::string user_prompt = build_user_prompt(text, target_language);
    return generate(format_chat_prompt(user_prompt), on_token, should_cancel);
}

std::string LocalRuntime::backend_label() const {
    return Impl::s_backend_label();
}

std::vector<std::uint8_t> ITranslationRuntime::read_file(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("failed to open model file: " + path.string());
    input.seekg(0, std::ios::end);
    const std::streamsize size = input.tellg();
    if (size <= 0) throw std::runtime_error("model file is empty: " + path.string());
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> data(static_cast<size_t>(size));
    if (!input.read(reinterpret_cast<char *>(data.data()), size))
        throw std::runtime_error("failed to read model file: " + path.string());
    return data;
}

std::string LocalRuntime::generate(
    const std::string &prompt,
    const std::function<void(const std::string &)> &on_token,
    const std::function<bool()> &should_cancel) {
    auto logger = spdlog::get("hymt");

    std::function<bool()> cancel_fn = should_cancel ? should_cancel : []() { return false; };
    llama_set_abort_callback(impl_->ctx_, abort_callback, &cancel_fn);

    struct AbortGuard {
        llama_context *ctx;
        ~AbortGuard() { llama_set_abort_callback(ctx, nullptr, nullptr); }
    } guard{impl_->ctx_};

    if (logger) {
        logger->trace("generate start, clearing memory");
        logger->trace("prompt: '{}'", prompt);
    }
    llama_memory_clear(llama_get_memory(impl_->ctx_), true);
    if (logger) logger->trace("memory cleared, resetting sampler");
    llama_sampler_reset(impl_->sampler_);

    const llama_vocab *vocab = llama_model_get_vocab(impl_->model_holder_.model);
    if (logger) logger->trace("tokenizing prompt (len={})", prompt.size());

    const int n_prompt = -llama_tokenize(
        vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
        nullptr, 0, true, true);
    if (logger) logger->trace("token count: {}", n_prompt);
    if (n_prompt <= 0) throw std::runtime_error("failed to measure prompt tokens");

    std::vector<llama_token> prompt_tokens(static_cast<size_t>(n_prompt));
    if (llama_tokenize(vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
                       prompt_tokens.data(), static_cast<int32_t>(prompt_tokens.size()),
                       true, true) < 0)
        throw std::runtime_error("failed to tokenize prompt");

    llama_batch batch = llama_batch_get_one(
        prompt_tokens.data(), static_cast<int32_t>(prompt_tokens.size()));
    std::string response;

    const int ctx_room = impl_->config_.n_ctx - n_prompt;
    if (ctx_room <= 0) throw std::runtime_error("prompt exceeds model context");
    const int max_gen = std::min(impl_->config_.max_tokens, ctx_room);

    if (logger)
        logger->debug("starting decode loop, max_gen={} prompt_tokens={} n_ctx={}",
                       max_gen, n_prompt, impl_->config_.n_ctx);

    core::Utf8StreamBuffer utf8_stream;
    for (int i = 0; i < max_gen; ++i) {
        if (i > 0 && i % 10 == 0 && logger)
            logger->trace("token {}/{}", i, max_gen);

        if (cancel_fn()) {
            if (logger) logger->debug("cancelled at token {}", i);
            throw std::runtime_error("translation cancelled");
        }

        const int decode_status = llama_decode(impl_->ctx_, batch);
        if (decode_status != 0) {
            if (cancel_fn()) throw std::runtime_error("translation cancelled");
            throw std::runtime_error("llama_decode failed");
        }

        const llama_token token = llama_sampler_sample(impl_->sampler_, impl_->ctx_, -1);
        if (llama_vocab_is_eog(vocab, token)) {
            if (logger) logger->debug("EOG at token {}", i);
            break;
        }

        const std::string piece = token_to_text(vocab, token);
        response.append(piece);
        if (logger) logger->trace("token {}:'{}'", i, piece);

        if (on_token) {
            const std::string emit = utf8_stream.push(piece);
            if (!emit.empty()) on_token(emit);
        }

        if (cancel_fn()) {
            if (logger) logger->debug("cancelled at token {}", i);
            throw std::runtime_error("translation cancelled");
        }
        batch = llama_batch_get_one(const_cast<llama_token *>(&token), 1);
    }

    if (on_token) {
        const std::string tail = utf8_stream.flush();
        if (!tail.empty()) on_token(tail);
    }

    if (logger) logger->trace("generate done, response_len={} response:'{}'", response.size(), response);

    write_ai_trace(prompt, response);
    return response;
}

}  // namespace qtrans::core
