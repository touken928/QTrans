#include "local_runtime.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace qtrans::core {

TEST(LocalRuntimeLifecycle, MinimalLoadRequestCarriesOnlyPathAndRuntimeConfig) {
    runtime_detail::ModelLoad load;
    EXPECT_TRUE(load.path.empty());
    EXPECT_EQ(load.generation.context_tokens, 4096);
    EXPECT_EQ(load.generation.max_output_tokens, 4096);

    load.path = std::filesystem::path("model.gguf");
    load.generation.context_tokens = 2048;
    load.generation.max_output_tokens = 1024;
    EXPECT_EQ(load.path, std::filesystem::path("model.gguf"));
    EXPECT_EQ(load.generation.context_tokens, 2048);
    EXPECT_EQ(load.generation.max_output_tokens, 1024);
}

TEST(LocalRuntimeLifecycle, UnloadPreservesSelectedBackendForReloadAttempt) {
    BackendState state;
    state.initialized = true;
    state.selected = Backend::Vulkan;
    state.label = "Vulkan";

    LocalRuntime runtime(state);
    EXPECT_EQ(runtime.backend(), Backend::Vulkan);

    runtime.unload();

    EXPECT_FALSE(runtime.loaded());
    EXPECT_EQ(runtime.backend(), Backend::Vulkan);

    runtime_detail::ModelLoad model;
    model.path = std::filesystem::path("controlled-missing-model.gguf");
    EXPECT_THROW(runtime.load(model), std::exception);
    EXPECT_EQ(runtime.backend(), Backend::Vulkan);
}

TEST(LocalRuntimeLifecycle, UnloadedRuntimeFailsFastBeforeTheLlamaBatchPath) {
    // The sampled-token storage fix in LocalRuntime::generate()
    // (llama_batch_get_one() borrows the last sampled token, which must
    // outlive the following llama_decode()) is only reachable through a real
    // llama model, which is unavailable here. Without a model the runtime must
    // still fail fast at the public boundary instead of touching llama state.
    LocalRuntime runtime(BackendState{});
    EXPECT_FALSE(runtime.loaded());
    EXPECT_EQ(runtime.count_prompt_tokens("hello"), 0);

    runtime_detail::GenerationOptions generation;
    runtime_detail::TokenSink on_token = [](std::string_view) {};
    runtime_detail::StopPredicate should_cancel = [] { return false; };
    LocalRuntime::GenerationStats stats;
    EXPECT_THROW(
        runtime.translate_with_stats("hello", generation, on_token, should_cancel, stats),
        std::exception);
    EXPECT_FALSE(runtime.loaded());
}

}  // namespace qtrans::core
