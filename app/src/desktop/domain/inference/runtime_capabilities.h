#pragma once

#include "qtrans/core.h"

#include <string>

class RuntimeCapabilities {
public:
    static RuntimeCapabilities &instance();

    void refresh(const qtrans::core::BackendState &environment);

    bool supports(qtrans::core::Backend backend) const;
    std::string describe(qtrans::core::Backend backend) const;
    const qtrans::core::BackendState &environment() const;

    friend struct RuntimeCapabilitiesTestAccess;

private:
    void set_support(qtrans::core::Backend backend, bool supported);

    qtrans::core::BackendState environment_{};
    bool gpu_vulkan_ = false;
    bool gpu_metal_ = false;
    bool refreshed_ = false;
};

struct RuntimeCapabilitiesTestAccess {
    static RuntimeCapabilities make_supported(
        std::initializer_list<qtrans::core::Backend> backends);
};
