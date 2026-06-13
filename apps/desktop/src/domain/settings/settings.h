#pragma once

#include "domain/storage/app_paths.h"
#include "domain/model-catalog/model_catalog.h"
#include "domain/inference/platform_profile.h"

#include <string>

class RuntimeCapabilities;

struct AppSettings {
    std::string models_dir;
    std::string model_id = default_model_id_for_platform();
    std::string hotkey = "Ctrl+`";
    int auto_close_ms = 5000;
    std::string source_language = "English";
    std::string target_language = "Chinese";
    std::string wordselect_source_language = "English";
    std::string wordselect_target_language = "Auto";
    bool wordselect_enabled = true;
    bool close_to_tray = true;

    void load(const AppPaths &paths);
    void save(const AppPaths &paths) const;
    void ensureStorage(const AppPaths &paths) const;

    const ModelCatalogEntry *selectedModel() const;
    std::string effectiveModelsDir(const AppPaths &paths) const;
    std::string effectiveModelPath(const AppPaths &paths) const;

    void setEffectiveModelsDir(const AppPaths &paths, const std::string &dir);
    void setSelectedModelId(const std::string &id);

    void migrateModelSelection(const RuntimeCapabilities &caps);
};
