#include "model/platform_profile.h"

#include "model/inference_resolver.h"
#include "model/model_catalog.h"
#include "model/runtime_capabilities.h"

#if defined(_WIN32)
#include <windows.h>
#endif

PlatformProfile detect_platform_profile() {
#if defined(__APPLE__) && (defined(__aarch64__) || defined(_M_ARM64))
    return PlatformProfile::Arm64;
#elif defined(_WIN32)
#if defined(_M_ARM64) || defined(__aarch64__)
    return PlatformProfile::Arm64;
#else
    return PlatformProfile::WindowsX64;
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
    return PlatformProfile::Arm64;
#else
    return PlatformProfile::Generic;
#endif
}

std::string default_model_id_for_platform() {
    return preferred_default_model_id(detect_platform_profile());
}

const char *preferred_default_model_id(PlatformProfile profile) {
    (void)profile;
    return "hymt2-q4";
}

const ModelCatalogEntry *default_available_model(
    const RuntimeCapabilities &caps,
    PlatformProfile profile) {
    if (const char *preferred_id = preferred_default_model_id(profile)) {
        if (const ModelCatalogEntry *preferred = find_model_by_id(preferred_id)) {
            if (resolve_inference(*preferred, caps)) {
                return preferred;
            }
        }
    }

    for (const ModelCatalogEntry &entry : model_catalog()) {
        if (resolve_inference(entry, caps)) {
            return &entry;
        }
    }

    return nullptr;
}
