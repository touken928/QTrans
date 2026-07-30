#include "local_runtime.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace qtrans::core {

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

    Model model;
    model.path = std::filesystem::path("controlled-missing-model.gguf");
    EXPECT_THROW(runtime.load(model), std::exception);
    EXPECT_EQ(runtime.backend(), Backend::Vulkan);
}

}  // namespace qtrans::core
