#pragma once

#include "qtrans/core/translation_model.h"

#include <filesystem>
#include <memory>

class HyMt2_7BLocalModel final : public qtrans::core::LocalGgufModelBase {
public:
    static std::unique_ptr<HyMt2_7BLocalModel> from_path(
        const std::filesystem::path &path,
        int n_gpu_layers);

    std::string build_user_prompt(const std::string &text,
                                  const std::string &target_language) const override;

    std::string format_chat_prompt(const std::string &user_prompt) const override;

private:
    explicit HyMt2_7BLocalModel(std::vector<std::uint8_t> weights,
                                qtrans::core::TranslatorOptions options);
};
