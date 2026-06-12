#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace qtrans::core {

enum class BackendType {
    Auto,
    Metal,
    Vulkan,
};

struct BackendOptions {
    BackendType backend_type = BackendType::Auto;
    std::filesystem::path plugin_dir;
};

struct ContextOptions {
    int n_ctx = 4096;
    int max_tokens = 4096;
};

struct GenerationOptions {
    float temperature = 0.7f;
    int top_k = 20;
    float top_p = 0.6f;
    float repeat_penalty = 1.05f;
};

struct TranslatorOptions {
    ContextOptions context;
    GenerationOptions generation;
    int n_gpu_layers = -1;
};

}  // namespace qtrans::core
