#include "prompt_profiles.h"

#include <sstream>

namespace qtrans::core::host_detail {
namespace {

constexpr const char k_7b_bos[] = "<|startoftext|>";
constexpr const char k_7b_end[] = "<|extra_0|>";
constexpr const char k_7b_extra4[] = "<|extra_4|>";
constexpr const char k_7b_eos[] = "<|eos|>";
constexpr const char k_18b_bos[] = u8"<\xEF\xBD\x9Chy_begin\xE2\x96\x81of\xE2\x96\x81sentence\xEF\xBD\x9C>";
constexpr const char k_18b_user[] = u8"<\xEF\xBD\x9Chy_User\xEF\xBD\x9C>";
constexpr const char k_18b_assistant[] = u8"<\xEF\xBD\x9Chy_Assistant\xEF\xBD\x9C>";

const char *role_name(Role role) {
    switch (role) {
        case Role::System:
            return "System";
        case Role::User:
            return "User";
        case Role::Assistant:
            return "Assistant";
    }
    return nullptr;
}

std::string translation_instruction(const TranslationInput &translation) {
    if (translation.target_language.value == "Auto") return "Translate the following segment:\n\n" + translation.text;
    const bool chinese_source = [&] {
        for (std::size_t i = 0; i + 2 < translation.text.size(); ++i) {
            const auto first = static_cast<unsigned char>(translation.text[i]);
            if (first >= 0xE0 && first <= 0xEF) {
                const std::uint32_t code_point = ((first & 0x0F) << 12) |
                                                 ((static_cast<unsigned char>(translation.text[i + 1]) & 0x3F) << 6) |
                                                 (static_cast<unsigned char>(translation.text[i + 2]) & 0x3F);
                if (code_point >= 0x4E00 && code_point <= 0x9FFF) return true;
            }
        }
        return false;
    }();
    const bool chinese_target = translation.target_language.value == "Chinese" ||
                                translation.target_language.value == "中文" ||
                                translation.target_language.value == "繁体中文" ||
                                translation.target_language.value == "Traditional Chinese" ||
                                translation.target_language.value == "zh" ||
                                translation.target_language.value == "zh-Hant";
    if (chinese_source && !chinese_target) {
        static const std::pair<const char *, const char *> names[] = {
            {"English", "英语"}, {"French", "法语"}, {"German", "德语"}, {"Japanese", "日语"}, {"Korean", "韩语"}, {"Spanish", "西班牙语"}, {"Arabic", "阿拉伯语"}, {"Russian", "俄语"}};
        std::string target = translation.target_language.value;
        for (const auto &[name, localized] : names)
            if (target == name) target = localized;
        return "将以下文本翻译为" + target + "，注意只需要输出翻译后的结果，不要额外解释：\n\n" + translation.text;
    }
    return "Translate the following segment into " + translation.target_language.value +
           ", without additional explanation.\n\n" + translation.text;
}

}  // namespace

Failure PromptProfile::render(const InvocationInput &input, std::string &prompt) const {
    if (const auto *translation = std::get_if<TranslationInput>(&input)) {
        if (translation->text.empty() || translation->target_language.value.empty())
            return {FailureCode::InvalidRequest, "translation input is incomplete"};
        const std::string instruction = translation_instruction(*translation);
        if (id == PromptProfileId::Hymt2SevenB)
            prompt = std::string(k_7b_bos) + instruction + k_7b_end;
        else
            prompt = std::string(k_18b_bos) + k_18b_user + instruction + k_18b_assistant;
        return {};
    }

    if (!supports_conversation) return {FailureCode::UnsupportedCapability, "model does not support conversation input"};
    const auto &conversation = std::get<ConversationInput>(input);
    if (conversation.messages.empty()) return {FailureCode::InvalidRequest, "conversation is empty"};

    // Official Hy-MT2-7B multi-turn template: a system turn emits
    // "<|startoftext|>{content}<|extra_4|>"; the user turn that immediately
    // follows it continues with "{content}<|extra_0|>" (no second BOS); all
    // later user turns emit "<|startoftext|>{content}<|extra_0|>"; assistant
    // turns emit "{content}<|eos|>". The final user turn's "<|extra_0|>" is
    // left as the generation prompt. No synthetic system content or history
    // trimming is applied.
    if (id == PromptProfileId::Hymt2SevenB) {
        std::string rendered;
        bool previous_was_system = false;
        for (const Message &message : conversation.messages) {
            if (message.content.empty() || role_name(message.role) == nullptr)
                return {FailureCode::InvalidRequest, "conversation contains an invalid message"};
            switch (message.role) {
                case Role::System:
                    rendered += k_7b_bos;
                    rendered += message.content;
                    rendered += k_7b_extra4;
                    previous_was_system = true;
                    break;
                case Role::User:
                    if (!previous_was_system) rendered += k_7b_bos;
                    rendered += message.content;
                    rendered += k_7b_end;
                    previous_was_system = false;
                    break;
                case Role::Assistant:
                    rendered += message.content;
                    rendered += k_7b_eos;
                    previous_was_system = false;
                    break;
            }
        }
        prompt = std::move(rendered);
        return {};
    }

    std::ostringstream rendered;
    rendered << k_18b_bos;
    for (const Message &message : conversation.messages) {
        if (message.content.empty() || role_name(message.role) == nullptr)
            return {FailureCode::InvalidRequest, "conversation contains an invalid message"};
        if (message.role == Role::System)
            rendered << k_18b_user << "System instructions:\n"
                     << message.content;
        else if (message.role == Role::User)
            rendered << k_18b_user << message.content;
        else
            rendered << k_18b_assistant << message.content;
    }
    rendered << k_18b_assistant;
    prompt = rendered.str();
    return {};
}

Failure select_prompt_profile(const ModelId &model, PromptProfile &profile) {
    if (model.value == "hymt2-7b-q4") {
        profile = {PromptProfileId::Hymt2SevenB, 8192, 1024, true};
        return {};
    }
    if (model.value == "hymt2-1.8b-q4") {
        profile = {PromptProfileId::Hymt2EighteenB, 4096, 1024, true};
        return {};
    }
    return {FailureCode::UnsupportedModel, "no prompt profile is known for model " + model.value};
}

}  // namespace qtrans::core::host_detail
