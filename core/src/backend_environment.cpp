#include "qtrans/core/backend_environment.h"

#include "ggml-backend.h"
#include "llama.h"

#include <spdlog/spdlog.h>

#include <mutex>
#include <string>
#include <string_view>

namespace qtrans::core {

namespace {

std::string &backend_label_storage() {
    static std::string label = "CPU";
    return label;
}

bool &vulkan_available() {
    static bool available = false;
    return available;
}

bool &metal_available() {
    static bool available = false;
    return available;
}

bool probe_vulkan_gpu() {
#ifdef QTRANS_MULTI_BACKEND
    const ggml_backend_reg_t reg = ggml_backend_reg_by_name("Vulkan");
    if (reg == nullptr) return false;
    const size_t device_count = ggml_backend_reg_dev_count(reg);
    for (size_t i = 0; i < device_count; ++i) {
        const ggml_backend_dev_t device = ggml_backend_reg_dev_get(reg, i);
        if (ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_GPU) return true;
    }
    const ggml_backend_dev_t gpu = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU);
    return gpu != nullptr;
#else
    return false;
#endif
}

bool probe_metal_gpu() {
#ifdef QTRANS_GPU_METAL
    const ggml_backend_reg_t reg = ggml_backend_reg_by_name("MTL");
    if (reg == nullptr) return false;
    const size_t device_count = ggml_backend_reg_dev_count(reg);
    for (size_t i = 0; i < device_count; ++i) {
        const ggml_backend_dev_t device = ggml_backend_reg_dev_get(reg, i);
        if (ggml_backend_dev_type(device) == GGML_BACKEND_DEVICE_TYPE_GPU) return true;
    }
    const ggml_backend_dev_t gpu = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU);
    return gpu != nullptr;
#else
    return false;
#endif
}

void log_llama_text(ggml_log_level level, const char *text) {
    if (text == nullptr) return;
    std::string_view message(text);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
        message.remove_suffix(1);
    }
    if (message.empty()) return;

    auto logger = spdlog::get("hymt");
    if (!logger) return;
    switch (level) {
        case GGML_LOG_LEVEL_ERROR:
            logger->error("{}", message);
            break;
#ifndef NDEBUG
        case GGML_LOG_LEVEL_WARN:
            logger->warn("{}", message);
            break;
        case GGML_LOG_LEVEL_INFO:
            logger->info("{}", message);
            break;
        case GGML_LOG_LEVEL_DEBUG:
            logger->debug("{}", message);
            break;
#endif
        default:
            break;
    }
}

void set_llama_log_callback() {
    llama_log_set([](ggml_log_level level, const char *text, void *) { log_llama_text(level, text); },
                  nullptr);
}

}  // namespace

void BackendEnvironment::initialize(const BackendOptions &options) {
    static std::once_flag once;
    std::call_once(once, [&options]() {
        set_llama_log_callback();
        llama_backend_init();
#ifdef QTRANS_MULTI_BACKEND
        ggml_backend_load_all();
        if (!options.plugin_dir.empty()) {
            ggml_backend_load_all_from_path(options.plugin_dir.string().c_str());
        }
#endif
        vulkan_available() = probe_vulkan_gpu();
        metal_available() = probe_metal_gpu();
        if (vulkan_available()) {
            backend_label_storage() = "Vulkan";
        } else if (metal_available()) {
            backend_label_storage() = "Metal";
        } else {
            backend_label_storage() = "CPU";
        }
    });
}

std::string BackendEnvironment::backend_label() {
    return backend_label_storage();
}

bool BackendEnvironment::has_vulkan() {
    return vulkan_available();
}

bool BackendEnvironment::has_metal() {
    return metal_available();
}

}  // namespace qtrans::core
