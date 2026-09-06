#include <gtest/gtest.h>

#include "qtrans/core.h"

#include <stdexcept>

using namespace qtrans::core;

TEST(CoreBackend, BackendEnumIsUnified) {
    EXPECT_NE(Backend::Automatic, Backend::Cpu);
    EXPECT_NE(Backend::Metal, Backend::Vulkan);
}

TEST(CoreBackend, BackendStateReportsResolvedSelection) {
    const BackendState state = initialize_backend(Backend::Automatic);
    EXPECT_TRUE(state.initialized);
    EXPECT_EQ(backend_state().selected, state.selected);
    EXPECT_FALSE(state.label.empty());
}

TEST(CoreBackend, ExplicitUnavailableBackendDoesNotPoisonAutomaticSelection) {
    const BackendState automatic = initialize_backend(Backend::Automatic);
    Backend unavailable = automatic.capabilities.metal_available ? Backend::Vulkan : Backend::Metal;
    if ((unavailable == Backend::Metal && automatic.capabilities.metal_available) ||
        (unavailable == Backend::Vulkan && automatic.capabilities.vulkan_available)) {
        GTEST_SKIP() << "No unavailable explicit backend on this platform";
    }
    EXPECT_THROW({ initialize_backend(unavailable); }, std::runtime_error);
    EXPECT_EQ(initialize_backend(Backend::Automatic).selected, automatic.selected);
}
