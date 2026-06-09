#pragma once

#include "model/inference_backend.h"

#include <filesystem>
#include <string>

class RuntimeCapabilities {
public:
    static RuntimeCapabilities &instance();

    void set_plugin_dir(std::filesystem::path plugin_dir);
    void refresh();

    bool supports(InferenceBackend backend) const;
    std::string describe(InferenceBackend backend) const;

    friend struct RuntimeCapabilitiesTestAccess;

private:
    void set_support(InferenceBackend backend, bool supported);

    std::filesystem::path plugin_dir_;
    bool cpu_stq1_0_ = true;
    bool cpu_ggml_ = true;
    bool gpu_vulkan_ = false;
    bool refreshed_ = false;
};

struct RuntimeCapabilitiesTestAccess {
    static RuntimeCapabilities make_supported(std::initializer_list<InferenceBackend> backends);
};
