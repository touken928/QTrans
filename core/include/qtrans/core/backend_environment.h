#pragma once

#include "qtrans/core/options.h"

namespace qtrans::core {

class BackendEnvironment {
public:
    static void initialize(const BackendOptions &options);
    static std::string backend_label();
    static bool has_vulkan();
    static bool has_metal();
};

}  // namespace qtrans::core
