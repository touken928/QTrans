#include "domain/model-catalog/model_catalog.h"
#include "domain/inference/platform_profile.h"
#include "domain/inference/runtime_capabilities.h"

#include <gtest/gtest.h>

TEST(PlatformProfile, PreferredDefaultModelIdIsQ4OnAllProfiles) {
    EXPECT_STREQ(preferred_default_model_id(PlatformProfile::WindowsX64), "hymt2-q4");
    EXPECT_STREQ(preferred_default_model_id(PlatformProfile::Arm64), "hymt2-q4");
    EXPECT_STREQ(preferred_default_model_id(PlatformProfile::Generic), "hymt2-q4");
}

TEST(PlatformProfile, DefaultAvailableModelPrefersQ4) {
    const RuntimeCapabilities caps = RuntimeCapabilitiesTestAccess::make_supported({
        qtrans::core::Backend::Vulkan,
        qtrans::core::Backend::Metal,
    });

    const ModelCatalogEntry *entry =
        default_available_model(caps, PlatformProfile::WindowsX64);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->id, "hymt2-q4");
}

TEST(PlatformProfile, DefaultAvailableModelUsesMetalOnArm64) {
    const RuntimeCapabilities caps =
        RuntimeCapabilitiesTestAccess::make_supported({qtrans::core::Backend::Metal});

    const ModelCatalogEntry *entry = default_available_model(caps, PlatformProfile::Arm64);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->id, "hymt2-q4");
}

TEST(PlatformProfile, DefaultAvailableModelUsesVulkanOnWin64) {
    const RuntimeCapabilities caps =
        RuntimeCapabilitiesTestAccess::make_supported({qtrans::core::Backend::Vulkan});

    const ModelCatalogEntry *entry =
        default_available_model(caps, PlatformProfile::WindowsX64);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->id, "hymt2-q4");
}
