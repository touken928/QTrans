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

struct TranslatorOptions {
    int n_ctx = 4096;
    int max_tokens = 4096;
    int n_gpu_layers = -1;
    float temperature = 0.7f;
    int top_k = 20;
    float top_p = 0.6f;
    float repeat_penalty = 1.05f;
};

struct PromptOptions {
    // Empty means use Hy-MT default templates.
    std::string user_prompt_template;
    std::string chat_prompt_template;
    std::string system_prompt;
};

}  // namespace qtrans::core
