#include "translation/translation_model.h"

#include <cstdio>
#include <stdexcept>
#include <utility>

namespace {

FILE *open_memory_buffer_as_file(std::vector<std::uint8_t> &buffer) {
#if defined(_WIN32)
    FILE *file = std::tmpfile();
    if (file == nullptr) {
        return nullptr;
    }

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

}  // namespace

LlamaModelFromMemory::~LlamaModelFromMemory() {
    if (model != nullptr) {
        llama_model_free(model);
        model = nullptr;
    }
    if (file != nullptr) {
        std::fclose(file);
        file = nullptr;
    }
}

LlamaModelFromMemory::LlamaModelFromMemory(LlamaModelFromMemory &&other) noexcept
    : buffer(std::move(other.buffer)),
      file(other.file),
      model(other.model) {
    other.file = nullptr;
    other.model = nullptr;
}

LlamaModelFromMemory &LlamaModelFromMemory::operator=(LlamaModelFromMemory &&other) noexcept {
    if (this != &other) {
        this->~LlamaModelFromMemory();
        buffer = std::move(other.buffer);
        file = other.file;
        model = other.model;
        other.file = nullptr;
        other.model = nullptr;
    }
    return *this;
}

LlamaModelFromMemory load_llama_model_from_memory(
    const std::vector<std::uint8_t> &data,
    const llama_model_params &params) {
    if (data.empty()) {
        throw std::invalid_argument("gguf data is empty");
    }

    LlamaModelFromMemory holder{};
    holder.buffer = data;

    holder.file = open_memory_buffer_as_file(holder.buffer);
    if (holder.file == nullptr) {
        throw std::runtime_error("failed to open gguf memory buffer");
    }

    llama_model_params model_params = params;
    model_params.use_mmap = false;

    holder.model = llama_model_load_from_file_ptr(holder.file, model_params);
    if (holder.model == nullptr) {
        throw std::runtime_error("failed to load llama model from memory");
    }

    return std::move(holder);
}
