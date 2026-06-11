#pragma once

#include "qtrans/core/translation_model.h"

#include <filesystem>
#include <memory>

class HyMt2_18BLocalModel final : public qtrans::core::LocalGgufModelBase {
public:
    static std::unique_ptr<HyMt2_18BLocalModel> from_path(
        const std::filesystem::path &path,
        int n_gpu_layers);

    qtrans::core::PromptFormatterPtr prompt_formatter() const override;

private:
    HyMt2_18BLocalModel(std::vector<std::uint8_t> weights,
                        qtrans::core::TranslatorOptions options,
                        qtrans::core::PromptFormatterPtr formatter);

    qtrans::core::PromptFormatterPtr formatter_;
};
