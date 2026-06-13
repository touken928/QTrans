#pragma once

#include "qtrans/core/backend_environment.h"

#include <string>

class RuntimeCapabilities {
public:
    static RuntimeCapabilities &instance();

    void refresh(const qtrans::core::ResolvedBackendEnvironment &environment);

    bool supports(qtrans::core::BackendKind backend) const;
    std::string describe(qtrans::core::BackendKind backend) const;
    const qtrans::core::ResolvedBackendEnvironment &environment() const;

    friend struct RuntimeCapabilitiesTestAccess;

private:
    void set_support(qtrans::core::BackendKind backend, bool supported);

    qtrans::core::ResolvedBackendEnvironment environment_{};
    bool gpu_vulkan_ = false;
    bool gpu_metal_ = false;
    bool refreshed_ = false;
};

struct RuntimeCapabilitiesTestAccess {
    static RuntimeCapabilities make_supported(
        std::initializer_list<qtrans::core::BackendKind> backends);
};
