#include "model/inference_resolver.h"
#include "model/model_catalog.h"
#include "model/runtime_capabilities.h"

#include <gtest/gtest.h>

namespace {

const ModelCatalogEntry *q4_model() {
    return find_model_by_id("hymt2-q4");
}

}  // namespace

TEST(InferenceResolver, Q4UsesVulkanWhenAvailable) {
    ASSERT_NE(q4_model(), nullptr);
    const RuntimeCapabilities caps =
        RuntimeCapabilitiesTestAccess::make_supported({InferenceBackend::GpuVulkan});

    const std::optional<ResolvedInference> resolved = resolve_inference(*q4_model(), caps);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->backend, InferenceBackend::GpuVulkan);
    EXPECT_EQ(resolved->n_gpu_layers, -1);
}

TEST(InferenceResolver, Q4UsesMetalWhenAvailable) {
    ASSERT_NE(q4_model(), nullptr);
    const RuntimeCapabilities caps =
        RuntimeCapabilitiesTestAccess::make_supported({InferenceBackend::GpuMetal});

    const std::optional<ResolvedInference> resolved = resolve_inference(*q4_model(), caps);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->backend, InferenceBackend::GpuMetal);
    EXPECT_EQ(resolved->n_gpu_layers, -1);
}

TEST(InferenceResolver, Q4PrefersVulkanOverMetalWhenBothAvailable) {
    ASSERT_NE(q4_model(), nullptr);
    const RuntimeCapabilities caps = RuntimeCapabilitiesTestAccess::make_supported({
        InferenceBackend::GpuVulkan,
        InferenceBackend::GpuMetal,
    });

    const std::optional<ResolvedInference> resolved = resolve_inference(*q4_model(), caps);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->backend, InferenceBackend::GpuVulkan);
}

TEST(InferenceResolver, Q4UnavailableWithoutAnyBackend) {
    ASSERT_NE(q4_model(), nullptr);
    const RuntimeCapabilities caps = RuntimeCapabilitiesTestAccess::make_supported({});

    EXPECT_FALSE(resolve_inference(*q4_model(), caps).has_value());
}
