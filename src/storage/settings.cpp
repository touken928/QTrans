#include "storage/settings.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {

std::string trim(const std::string & value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(start, end - start + 1);
}

void migrate_legacy_settings(AppSettings & settings, const AppPaths & paths) {
    if (find_model_by_id(settings.model_id) == nullptr) {
        settings.model_id = default_model()->id;
    }

    if (!settings.models_dir.empty()) {
        return;
    }

    std::ifstream input(paths.settings_file);
    if (!input) {
        return;
    }

    std::string legacy_model_path;
    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        const auto sep = line.find('=');
        if (sep == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, sep));
        const std::string value = trim(line.substr(sep + 1));
        if (key == "model_path") {
            legacy_model_path = value;
        }
    }

    if (legacy_model_path.empty()) {
        return;
    }

    const std::filesystem::path configured(legacy_model_path);
    if (configured.has_parent_path()) {
        settings.models_dir = configured.parent_path().string();
    }

    if (const ModelCatalogEntry * entry = find_model_by_filename(configured.string())) {
        settings.model_id = entry->id;
    }
}

} // namespace

void AppSettings::load(const AppPaths & paths) {
    paths.ensureDirectories();

    std::ifstream input(paths.settings_file);
    if (!input) {
        return;
    }

    std::string line;
    while (std::getline(input, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const auto sep = line.find('=');
        if (sep == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, sep));
        const std::string value = trim(line.substr(sep + 1));

        if (key == "models_dir") {
            models_dir = value;
        } else if (key == "model_id") {
            model_id = value;
        } else if (key == "hotkey") {
            hotkey = value;
        } else if (key == "auto_close_ms") {
            auto_close_ms = std::stoi(value);
        } else if (key == "source_language") {
            source_language = value;
        } else if (key == "target_language") {
            target_language = value;
        }
    }

    migrate_legacy_settings(*this, paths);
}

void AppSettings::save(const AppPaths & paths) const {
    paths.ensureDirectories();

    std::ofstream output(paths.settings_file, std::ios::trunc);
    if (!output) {
        throw std::runtime_error("failed to write settings: " + paths.settings_file.string());
    }

    output << "models_dir=" << models_dir << '\n';
    output << "model_id=" << model_id << '\n';
    output << "hotkey=" << hotkey << '\n';
    output << "auto_close_ms=" << auto_close_ms << '\n';
    output << "source_language=" << source_language << '\n';
    output << "target_language=" << target_language << '\n';
}

void AppSettings::ensureStorage(const AppPaths & paths) const {
    paths.ensureDirectories();

    std::error_code ec;
    std::filesystem::create_directories(effectiveModelsDir(paths), ec);
}

const ModelCatalogEntry * AppSettings::selectedModel() const {
    if (const ModelCatalogEntry * entry = find_model_by_id(model_id)) {
        return entry;
    }
    return default_model();
}

std::string AppSettings::effectiveModelsDir(const AppPaths & paths) const {
    if (models_dir.empty()) {
        return paths.models_dir.string();
    }

    const std::filesystem::path configured(models_dir);
    if (configured.is_absolute()) {
        return configured.string();
    }

    return (paths.data_root / configured).string();
}

std::string AppSettings::effectiveModelPath(const AppPaths & paths) const {
    const ModelCatalogEntry * entry = selectedModel();
    return (std::filesystem::path(effectiveModelsDir(paths)) / entry->filename).string();
}

void AppSettings::setEffectiveModelsDir(const AppPaths & paths, const std::string & dir) {
    const std::string trimmed = trim(dir);
    if (trimmed.empty() || trimmed == paths.models_dir.string()) {
        models_dir.clear();
        return;
    }

    const std::filesystem::path configured(trimmed);
    if (configured.is_absolute()) {
        models_dir = configured.string();
        return;
    }

    models_dir = configured.string();
}

void AppSettings::setSelectedModelId(const std::string & id) {
    if (find_model_by_id(id) != nullptr) {
        model_id = id;
    }
}
