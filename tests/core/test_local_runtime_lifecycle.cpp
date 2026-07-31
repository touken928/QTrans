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

}  // namespace qtrans::core
