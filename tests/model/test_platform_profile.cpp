#include "model/model_catalog.h"
#include "model/platform_profile.h"
#include "model/runtime_capabilities.h"

#include <gtest/gtest.h>

TEST(PlatformProfile, PreferredDefaultModelIds) {
    EXPECT_STREQ(preferred_default_model_id(PlatformProfile::WindowsX64), "hymt2-q4");
    EXPECT_STREQ(preferred_default_model_id(PlatformProfile::Arm64), "hymt2-125bit");
    EXPECT_STREQ(preferred_default_model_id(PlatformProfile::Generic), "hymt2-125bit");
}

TEST(PlatformProfile, DefaultAvailableModelPrefersPlatformChoice) {
    const RuntimeCapabilities caps = RuntimeCapabilitiesTestAccess::make_supported({
        InferenceBackend::GpuVulkan,
        InferenceBackend::CpuGgml,
        InferenceBackend::CpuStq1_0,
    });

    const ModelCatalogEntry *entry =
        default_available_model(caps, PlatformProfile::WindowsX64);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->id, "hymt2-q4");
}

TEST(PlatformProfile, DefaultAvailableModelUsesArm64Preference) {
    const RuntimeCapabilities caps = RuntimeCapabilitiesTestAccess::make_supported({
        InferenceBackend::CpuGgml,
        InferenceBackend::CpuStq1_0,
    });

    const ModelCatalogEntry *entry = default_available_model(caps, PlatformProfile::Arm64);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->id, "hymt2-125bit");
}

TEST(PlatformProfile, Win64WithoutVulkanStillGetsQ4OnCpu) {
    const RuntimeCapabilities caps = RuntimeCapabilitiesTestAccess::make_supported({
        InferenceBackend::CpuGgml,
        InferenceBackend::CpuStq1_0,
    });

    const ModelCatalogEntry *entry =
        default_available_model(caps, PlatformProfile::WindowsX64);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->id, "hymt2-q4");
}
