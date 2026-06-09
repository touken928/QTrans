#include "model/inference_resolver.h"
#include "model/model_catalog.h"
#include "model/runtime_capabilities.h"

#include <gtest/gtest.h>

namespace {

const ModelCatalogEntry *q4_model() {
    return find_model_by_id("hymt2-q4");
}

const ModelCatalogEntry *stq_model() {
    return find_model_by_id("hymt2-125bit");
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

TEST(InferenceResolver, Q4FallsBackToCpuGgml) {
    ASSERT_NE(q4_model(), nullptr);
    const RuntimeCapabilities caps =
        RuntimeCapabilitiesTestAccess::make_supported({InferenceBackend::CpuGgml});

    const std::optional<ResolvedInference> resolved = resolve_inference(*q4_model(), caps);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->backend, InferenceBackend::CpuGgml);
    EXPECT_EQ(resolved->n_gpu_layers, 0);
}

TEST(InferenceResolver, StqUsesCpuStq1_0) {
    ASSERT_NE(stq_model(), nullptr);
    const RuntimeCapabilities caps =
        RuntimeCapabilitiesTestAccess::make_supported({InferenceBackend::CpuStq1_0});

    const std::optional<ResolvedInference> resolved = resolve_inference(*stq_model(), caps);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->backend, InferenceBackend::CpuStq1_0);
    EXPECT_EQ(resolved->n_gpu_layers, 0);
}

TEST(InferenceResolver, StqUnavailableWithoutCpuStq) {
    ASSERT_NE(stq_model(), nullptr);
    const RuntimeCapabilities caps =
        RuntimeCapabilitiesTestAccess::make_supported({InferenceBackend::CpuGgml});

    EXPECT_FALSE(resolve_inference(*stq_model(), caps).has_value());
    EXPECT_FALSE(unavailable_reason(*stq_model(), caps).empty());
}

TEST(InferenceResolver, Q4UnavailableWithoutAnyBackend) {
    ASSERT_NE(q4_model(), nullptr);
    const RuntimeCapabilities caps = RuntimeCapabilitiesTestAccess::make_supported({});

    EXPECT_FALSE(resolve_inference(*q4_model(), caps).has_value());
}
