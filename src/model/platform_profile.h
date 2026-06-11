#pragma once

#include <string>

struct ModelCatalogEntry;
class RuntimeCapabilities;

enum class PlatformProfile {
    WindowsX64,
    Arm64,
    Generic,
};

PlatformProfile detect_platform_profile();

const char *preferred_default_model_id(PlatformProfile profile);

std::string default_model_id_for_platform();

const ModelCatalogEntry *default_available_model(
    const RuntimeCapabilities &caps,
    PlatformProfile profile);
