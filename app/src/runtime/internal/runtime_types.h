#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>

namespace qtrans::core::runtime_detail {

// Internalized local load/generation contracts formerly exposed through the
// public synchronous facade. These types are owned by LocalRuntime and
// ModelRuntime only; the public core boundary exposes ModelHost and backend
// state exclusively.

struct GenerationOptions {
    int context_tokens = 4096;
    int max_output_tokens = 4096;
    float temperature = 0.7f;
    int top_k = 20;
    float top_p = 0.6f;
    float repeat_penalty = 1.05f;
    std::uint32_t seed = 0;
};

// Minimal local load request: model file location plus the runtime
// configuration used to size and sample the llama context.
struct ModelLoad {
    std::filesystem::path path;
    GenerationOptions generation;
};

using TokenSink = std::function<void(std::string_view)>;
using StopPredicate = std::function<bool()>;

}  // namespace qtrans::core::runtime_detail
