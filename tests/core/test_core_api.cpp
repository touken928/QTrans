#include <gtest/gtest.h>

#include "qtrans/core.h"

#include <stdexcept>

using namespace qtrans::core;

TEST(CoreFacade, BackendEnumIsUnified) {
    EXPECT_NE(Backend::Automatic, Backend::Cpu);
    EXPECT_NE(Backend::Metal, Backend::Vulkan);
}

TEST(CoreFacade, TranslatorIsMoveOnlyAndNotLoadedInitially) {
    Translator translator(Backend::Automatic);
    EXPECT_FALSE(translator.loaded());
    EXPECT_EQ(translator.backend(), backend_state().selected);
    EXPECT_EQ(translator.translate({"text", "English", OverflowPolicy::Split}).error_code,
              TranslationErrorCode::NotLoaded);
}

TEST(CoreFacade, ModelRequiresPathAndFormatter) {
    Translator translator(Backend::Automatic);
    Model model;
    model.prompt_formatter = [](std::string_view text, std::string_view) { return std::string(text); };
    EXPECT_THROW(translator.load(model), std::invalid_argument);
}

TEST(CoreFacade, FormatterStringViewIsConsumedDuringCall) {
    Model model;
    model.path = "/tmp/model.gguf";
    model.prompt_formatter = [](std::string_view text, std::string_view language) {
        return std::string(language) + ":" + std::string(text);
    };
    EXPECT_EQ(model.prompt_formatter("hello", "Chinese"), "Chinese:hello");
}

TEST(CoreFacade, ExplicitUnavailableBackendDoesNotPoisonAutomaticSelection) {
    const BackendState automatic = initialize_backend(Backend::Automatic);
    Backend unavailable = automatic.capabilities.metal_available ? Backend::Vulkan : Backend::Metal;
    if ((unavailable == Backend::Metal && automatic.capabilities.metal_available) ||
        (unavailable == Backend::Vulkan && automatic.capabilities.vulkan_available)) {
        GTEST_SKIP() << "No unavailable explicit backend on this platform";
    }
    EXPECT_THROW({ Translator candidate(unavailable); }, std::runtime_error);
    EXPECT_EQ(Translator(Backend::Automatic).backend(), automatic.selected);
}
