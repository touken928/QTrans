#include "domain/inference/inference_resolver.h"
#include "domain/model-catalog/model_catalog.h"
#include "domain/inference/runtime_capabilities.h"

#include <gtest/gtest.h>

namespace {

const ModelCatalogEntry *q4_model() {
    return find_model_by_id("hymt2-1.8b-q4");
}

}  // namespace

TEST(InferenceResolver, Q4UsesVulkanWhenAvailable) {
    ASSERT_NE(q4_model(), nullptr);
    const RuntimeCapabilities caps =
        RuntimeCapabilitiesTestAccess::make_supported({qtrans::core::Backend::Vulkan});

    const std::optional<ResolvedInference> resolved = resolve_inference(*q4_model(), caps);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->backend, qtrans::core::Backend::Vulkan);
}

TEST(InferenceResolver, Q4UsesMetalWhenAvailable) {
    ASSERT_NE(q4_model(), nullptr);
    const RuntimeCapabilities caps =
        RuntimeCapabilitiesTestAccess::make_supported({qtrans::core::Backend::Metal});

    const std::optional<ResolvedInference> resolved = resolve_inference(*q4_model(), caps);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->backend, qtrans::core::Backend::Metal);
}

TEST(InferenceResolver, Q4PrefersVulkanOverMetalWhenBothAvailable) {
    ASSERT_NE(q4_model(), nullptr);
    const RuntimeCapabilities caps = RuntimeCapabilitiesTestAccess::make_supported({
        qtrans::core::Backend::Vulkan,
        qtrans::core::Backend::Metal,
    });

    const std::optional<ResolvedInference> resolved = resolve_inference(*q4_model(), caps);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->backend, qtrans::core::Backend::Vulkan);
}

TEST(InferenceResolver, Q4UnavailableWithoutAnyBackend) {
    ASSERT_NE(q4_model(), nullptr);
    const RuntimeCapabilities caps = RuntimeCapabilitiesTestAccess::make_supported({});

    EXPECT_FALSE(resolve_inference(*q4_model(), caps).has_value());
}
