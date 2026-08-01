#include "local_runtime.h"

#include "diagnostics.h"

#include "translation_control.h"
#include "text/utf8_stream_buffer.h"

#include "ggml-backend.h"
#include "llama.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <sstream>
#include <new>
#include <memory>
#include <stdexcept>
#include <vector>

namespace qtrans::core {

namespace {

const char *backend_registry_name(Backend backend) {
    switch (backend) {
        case Backend::Metal:
            return "MTL";
        case Backend::Vulkan:
            return "Vulkan";
        case Backend::Cpu:
        default:
            return nullptr;
    }
}

std::vector<ggml_backend_dev_t> selected_backend_devices(const BackendState &environment) {
    std::vector<ggml_backend_dev_t> devices;
    const char *reg_name = backend_registry_name(environment.selected);
    if (reg_name == nullptr) {
        return devices;
    }

    const ggml_backend_reg_t reg = ggml_backend_reg_by_name(reg_name);
    if (reg == nullptr) {
        throw std::runtime_error(std::string("backend registry not found for ") + environment.label);
    }

    const size_t device_count = ggml_backend_reg_dev_count(reg);
    for (size_t i = 0; i < device_count; ++i) {
        const ggml_backend_dev_t device = ggml_backend_reg_dev_get(reg, i);
        if (ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_GPU) {
            devices.push_back(device);
        }
    }

    if (devices.empty()) {
        throw std::runtime_error("resolved backend has no GPU devices");
    }

    devices.push_back(nullptr);
    return devices;
}

std::string describe_device(ggml_backend_dev_t device) {
    ggml_backend_dev_props props{};
    ggml_backend_dev_get_props(device, &props);

    std::ostringstream out;
    out << (props.name != nullptr ? props.name : "unknown-device");
    if (props.description != nullptr && props.description[0] != '\0') {
        out << " (" << props.description << ")";
    }
    if (props.memory_total > 0) {
        out << ", memory=" << (props.memory_total / (1024 * 1024)) << " MiB";
    }
    return out.str();
}

std::string describe_device_list(const std::vector<ggml_backend_dev_t> &devices) {
    std::ostringstream out;
    bool first = true;
    for (ggml_backend_dev_t device : devices) {
        if (device == nullptr) {
            continue;
        }
        if (!first) {
            out << "; ";
        }
        first = false;
        out << describe_device(device);
    }
    if (first) {
        return "none";
    }
    return out.str();
}

void emit_hymt_message(qtrans::core::DiagnosticLevel level, const std::string &message) {
    qtrans::core::diagnostics::emit(level, "local_runtime", message);
}

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
        reset();
    }
    void reset() noexcept {
        if (model != nullptr) {
            llama_model_free(model);
            model = nullptr;
        }
        if (file != nullptr) {
            std::fclose(file);
            file = nullptr;
        }
        buffer.clear();
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
            if (model != nullptr) {
                llama_model_free(model);
            }
            if (file != nullptr) {
                std::fclose(file);
            }
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
    if (written != buffer.size()) {
        std::fclose(file);
        return nullptr;
    }
    std::rewind(file);
    return file;
#else
    return fmemopen(buffer.data(), buffer.size(), "rb");
#endif
}

LlamaModelHolder load_llama_model(const std::filesystem::path &path,
                                  const llama_model_params &params) {
    if (path.empty()) throw std::invalid_argument("model path is empty");
    const auto path_utf8 = path.u8string();
    LlamaModelHolder holder;
    llama_model_params model_params = params;
    // leave use_mmap as default (true) for direct file loading
    holder.model = llama_model_load_from_file(
        reinterpret_cast<const char *>(path_utf8.c_str()), model_params);
    if (holder.model == nullptr)
        throw std::runtime_error("failed to load llama model from file: " + path.string());
    return holder;
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
// Sampler creation
// ---------------------------------------------------------------------------

llama_sampler *create_sampler(const runtime_detail::GenerationOptions &config) {
    llama_sampler *chain = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler_chain_add(chain, llama_sampler_init_penalties(-1, config.repeat_penalty, 0.0f, 0.0f));
    llama_sampler_chain_add(chain, llama_sampler_init_top_k(config.top_k));
    llama_sampler_chain_add(chain, llama_sampler_init_top_p(config.top_p, 1));
    llama_sampler_chain_add(chain, llama_sampler_init_temp(config.temperature));
    llama_sampler_chain_add(chain, llama_sampler_init_dist(config.seed));
    return chain;
}

// ---------------------------------------------------------------------------
// Token helpers
// ---------------------------------------------------------------------------

bool abort_callback(void *data) noexcept {
    if (data == nullptr) return false;
    auto *state = static_cast<runtime_detail::CallbackState *>(data);
    return state->requested();
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

}  // anonymous namespace

// ---------------------------------------------------------------------------
// LocalRuntime::Impl — holds all llama state
// ---------------------------------------------------------------------------

struct LocalRuntime::Impl {
    LlamaModelHolder model_holder_;
    llama_context *ctx_ = nullptr;
    llama_sampler *sampler_ = nullptr;
    runtime_detail::GenerationOptions config_;
    BackendState environment_{};
    std::string backend_label_ = "CPU";
    bool loaded_ = false;

    void unload_resources() noexcept {
        if (sampler_ != nullptr) {
            llama_sampler_free(sampler_);
            sampler_ = nullptr;
        }
        if (ctx_ != nullptr) {
            llama_free(ctx_);
            ctx_ = nullptr;
        }
        model_holder_.reset();
        config_ = {};
        loaded_ = false;
    }

    ~Impl() {
        unload_resources();
    }
};

// ---------------------------------------------------------------------------
// LocalRuntime implementation
// ---------------------------------------------------------------------------

LocalRuntime::LocalRuntime(BackendState environment)
    : impl_(std::make_unique<Impl>()) {
    impl_->environment_ = std::move(environment);
    impl_->backend_label_ = impl_->environment_.label;
}

LocalRuntime::~LocalRuntime() = default;

LocalRuntime::LocalRuntime(LocalRuntime &&other) noexcept
    : impl_(std::move(other.impl_)) {
}
LocalRuntime &LocalRuntime::operator=(LocalRuntime &&other) noexcept {
    if (this != &other) impl_ = std::move(other.impl_);
    return *this;
}

void LocalRuntime::load(const runtime_detail::ModelLoad &model) {
    if (model.path.empty()) throw std::invalid_argument("model path is empty");
    const runtime_detail::GenerationOptions &config = model.generation;

    auto candidate = std::make_unique<Impl>();
    candidate->environment_ = impl_->environment_;
    candidate->backend_label_ = impl_->backend_label_;
    candidate->config_ = config;

    // Clean up previous state.
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = candidate->environment_.selected == Backend::Cpu ? 0 : -1;
    std::vector<ggml_backend_dev_t> selected_devices;
    if (candidate->environment_.selected != Backend::Cpu) {
        selected_devices = selected_backend_devices(candidate->environment_);
        model_params.devices = selected_devices.data();
        model_params.main_gpu = 0;
        emit_hymt_message(DiagnosticLevel::Info,
                          "resolved backend=" + candidate->environment_.label +
                              ", selected devices=" + describe_device_list(selected_devices));
    } else {
        emit_hymt_message(DiagnosticLevel::Info,
                          "resolved backend=CPU, loading model without GPU device selection");
    }

    candidate->model_holder_ = load_llama_model(model.path, model_params);

    const int train_ctx = llama_model_n_ctx_train(candidate->model_holder_.model);
    if (train_ctx > 0 && candidate->config_.context_tokens > train_ctx) {
        candidate->config_.context_tokens = train_ctx;
        if (candidate->config_.max_output_tokens > train_ctx) {
            candidate->config_.max_output_tokens = train_ctx;
        }
    }

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = static_cast<uint32_t>(candidate->config_.context_tokens);
    ctx_params.n_batch = static_cast<uint32_t>(candidate->config_.context_tokens);

    candidate->ctx_ = llama_init_from_model(candidate->model_holder_.model, ctx_params);
    if (candidate->ctx_ == nullptr) {
        throw std::runtime_error("failed to create llama context");
    }

    emit_hymt_message(DiagnosticLevel::Info,
                      "llama context created for backend=" + impl_->backend_label_ +
                          ", gpu_layers=" + std::to_string(model_params.n_gpu_layers));

    candidate->loaded_ = true;
    impl_.swap(candidate);
}

void LocalRuntime::unload() noexcept {
    impl_->unload_resources();
}

bool LocalRuntime::loaded() const noexcept {
    return impl_->loaded_;
}

Backend LocalRuntime::backend() const noexcept {
    return impl_->environment_.selected;
}

int LocalRuntime::context_tokens() const noexcept {
    return impl_->config_.context_tokens;
}

int LocalRuntime::count_prompt_tokens(const std::string &prompt) const {
    if (!loaded()) return 0;
    const llama_vocab *vocab = llama_model_get_vocab(impl_->model_holder_.model);
    const int n_prompt = -llama_tokenize(
        vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
        nullptr, 0, false, true);
    return n_prompt > 0 ? n_prompt : 0;
}

std::string LocalRuntime::translate_with_stats(
    const std::string &prompt,
    const runtime_detail::GenerationOptions &generation,
    const runtime_detail::TokenSink &on_token,
    const runtime_detail::StopPredicate &should_cancel,
    GenerationStats &stats) {
    if (!loaded()) throw std::runtime_error("model is not loaded");
    return generate(prompt, generation, on_token, should_cancel, &stats);
}

std::string LocalRuntime::generate(
    const std::string &prompt,
    const runtime_detail::GenerationOptions &generation,
    const runtime_detail::TokenSink &on_token,
    const runtime_detail::StopPredicate &should_cancel,
    GenerationStats *stats) {
    runtime_detail::CallbackState callback_state;
    callback_state.checker = should_cancel;
    llama_set_abort_callback(impl_->ctx_, abort_callback, &callback_state);

    struct AbortGuard {
        llama_context *ctx;
        ~AbortGuard() {
            llama_set_abort_callback(ctx, nullptr, nullptr);
        }
    } guard{impl_->ctx_};

    emit_hymt_message(DiagnosticLevel::Trace, "generate start, clearing memory");
    emit_hymt_message(DiagnosticLevel::Trace, "prompt length=" + std::to_string(prompt.size()));
    llama_memory_clear(llama_get_memory(impl_->ctx_), true);
    emit_hymt_message(DiagnosticLevel::Trace, "memory cleared, creating invocation sampler");
    llama_sampler *sampler = create_sampler(generation);
    struct SamplerGuard {
        llama_sampler *sampler;
        ~SamplerGuard() {
            llama_sampler_free(sampler);
        }
    } sampler_guard{sampler};

    const llama_vocab *vocab = llama_model_get_vocab(impl_->model_holder_.model);
    emit_hymt_message(DiagnosticLevel::Trace, "tokenizing prompt");

    // Two-pass tokenization: the first call measures the required capacity
    // (llama_tokenize returns the negated count when the buffer is too small),
    // the second fills a buffer of exactly that size. The actual second-call
    // result is validated against the allocated capacity, the vector is
    // resized to the actual count, and that actual count drives the batch,
    // context budget, and token statistics. add_special=false and
    // parse_special=true are preserved.
    const int n_prompt_measured = -llama_tokenize(
        vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
        nullptr, 0, false, true);
    runtime_detail::check_stop(callback_state);
    emit_hymt_message(DiagnosticLevel::Trace,
                      "prompt token count=" + std::to_string(n_prompt_measured));
    if (n_prompt_measured <= 0) throw std::runtime_error("failed to measure prompt tokens");

    std::vector<llama_token> prompt_tokens(static_cast<size_t>(n_prompt_measured));
    const int n_prompt = llama_tokenize(
        vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
        prompt_tokens.data(), static_cast<int32_t>(prompt_tokens.size()), false, true);
    if (n_prompt < 0 || n_prompt > static_cast<int32_t>(prompt_tokens.size()))
        throw std::runtime_error("failed to tokenize prompt");
    prompt_tokens.resize(static_cast<size_t>(n_prompt));
    runtime_detail::check_stop(callback_state);

    if (stats != nullptr) stats->prompt_tokens = n_prompt;

    llama_batch batch = llama_batch_get_one(prompt_tokens.data(), n_prompt);
    std::string response;

    const int ctx_room = generation.context_tokens - n_prompt;
    if (ctx_room <= 0) throw std::runtime_error("prompt exceeds model context");
    const int max_gen = std::min(generation.max_output_tokens, ctx_room);

    emit_hymt_message(DiagnosticLevel::Debug,
                      "starting decode loop, max_gen=" + std::to_string(max_gen) +
                          " prompt_tokens=" + std::to_string(n_prompt) +
                          " n_ctx=" + std::to_string(generation.context_tokens));

    core::Utf8StreamBuffer utf8_stream;
    // Storage for the last sampled token. It must outlive the llama_decode()
    // call of the following iteration because llama_batch_get_one() borrows
    // the pointer instead of copying the token.
    llama_token last_token = 0;
    for (int i = 0; i < max_gen; ++i) {
        if (i > 0 && i % 10 == 0) {
            emit_hymt_message(DiagnosticLevel::Trace,
                              "token progress=" + std::to_string(i) + "/" + std::to_string(max_gen));
        }

        runtime_detail::check_stop(callback_state);

        const int decode_status = llama_decode(impl_->ctx_, batch);
        if (decode_status != 0) {
            runtime_detail::check_stop(callback_state);
            throw std::runtime_error("llama_decode failed");
        }
        runtime_detail::check_stop(callback_state);

        last_token = llama_sampler_sample(sampler, impl_->ctx_, -1);
        if (llama_vocab_is_eog(vocab, last_token)) {
            emit_hymt_message(DiagnosticLevel::Debug,
                              "EOG at token " + std::to_string(i));
            break;
        }

        const std::string piece = token_to_text(vocab, last_token);
        response.append(piece);
        if (stats != nullptr) stats->output_tokens = i + 1;

        if (on_token) {
            const std::string emit = utf8_stream.push(piece);
            if (!emit.empty()) {
                if (stats != nullptr) stats->output_text += emit;
                try {
                    on_token(emit);
                } catch (const runtime_detail::TranslationCallbackFailed &) {
                    throw;
                } catch (const std::exception &ex) {
                    throw runtime_detail::TranslationCallbackFailed(
                        std::string("token callback failed: ") + ex.what());
                } catch (...) {
                    throw runtime_detail::TranslationCallbackFailed("token callback failed");
                }
            }
        }

        runtime_detail::check_stop(callback_state);
        batch = llama_batch_get_one(&last_token, 1);
    }

    if (stats != nullptr) {
        stats->stopped_at_end = response.empty() || stats->output_tokens < max_gen;
        stats->reached_limit = stats->output_tokens >= max_gen;
    }

    if (on_token) {
        const std::string tail = utf8_stream.flush();
        if (!tail.empty()) {
            if (stats != nullptr) stats->output_text += tail;
            try {
                on_token(tail);
            } catch (const runtime_detail::TranslationCallbackFailed &) {
                throw;
            } catch (const std::exception &ex) {
                throw runtime_detail::TranslationCallbackFailed(
                    std::string("token callback failed: ") + ex.what());
            } catch (...) {
                throw runtime_detail::TranslationCallbackFailed("token callback failed");
            }
        }
    }

    emit_hymt_message(DiagnosticLevel::Trace,
                      "generate done, response_len=" + std::to_string(response.size()));

    diagnostics::emit_ai_trace(prompt, response);
    return stats != nullptr ? stats->output_text : response;
}

}  // namespace qtrans::core
