#include "model_runtime.h"

#include "../text/chunker.h"
#include "../text/utf8_stream_buffer.h"
#include "../translation_control.h"

#include <algorithm>
#include <chrono>

namespace qtrans::core::host_detail {

ModelRuntime::ModelRuntime(RuntimeInjection injection)
    : runtime_(injection ? BackendState{} : initialize_backend(Backend::Automatic)),
      injection_(std::move(injection)) {
}

Failure ModelRuntime::load(const ModelSpec &model) {
    Failure profile_failure = select_prompt_profile(model.id, profile_);
    if (profile_failure && !injection_) return profile_failure;
    if (profile_failure) profile_ = {PromptProfileId::Hymt2EighteenB, 4096, 1024, true};
    if (injection_) {
        loaded_ = true;
        return {};
    }
    runtime_detail::ModelLoad local_model;
    local_model.path = model.path;
    local_model.generation.context_tokens = profile_.context_tokens;
    local_model.generation.max_output_tokens = profile_.default_output_tokens;
    try {
        runtime_.load(std::move(local_model));
    } catch (const std::exception &error) {
        return {FailureCode::Runtime, error.what()};
    } catch (...) {
        return {FailureCode::Runtime, "model runtime load failed"};
    }
    profile_.context_tokens = runtime_.context_tokens();
    profile_.default_output_tokens = std::min(profile_.default_output_tokens, profile_.context_tokens - 1);
    loaded_ = true;
    return {};
}

Failure ModelRuntime::unload() noexcept {
    runtime_.unload();
    loaded_ = false;
    return {};
}

bool ModelRuntime::loaded() const noexcept {
    return loaded_;
}

const PromptProfile &ModelRuntime::profile() const noexcept {
    return profile_;
}

Failure ModelRuntime::execute_prompt(const std::string &prompt,
                                     const runtime_detail::GenerationOptions &generation,
                                     const RuntimeDeltaSink &on_delta,
                                     const RuntimeStopPredicate &should_stop,
                                     RuntimeExecution &result) {
    const int prompt_tokens = injection_ ? static_cast<int>(prompt.size()) : runtime_.count_prompt_tokens(prompt);
    const int output_reservation = generation.max_output_tokens;
    if (prompt_tokens <= 0 || prompt_tokens + output_reservation > profile_.context_tokens)
        return {FailureCode::ContextLimit, "prompt plus output reservation exceeds model context"};
    if (injection_) {
        result.prompt_tokens = prompt_tokens;
        core::Utf8StreamBuffer utf8;
        std::string emitted;
        const RuntimeDeltaSink safe_delta = [&](std::string_view piece) {
            const std::string valid = utf8.push(piece);
            if (!valid.empty()) {
                emitted += valid;
                if (on_delta) on_delta(valid);
            }
        };
        const Failure failure = injection_(prompt, generation, safe_delta, should_stop, result);
        if (!failure) {
            const std::string tail = utf8.flush();
            if (!tail.empty()) {
                emitted += tail;
                if (on_delta) on_delta(tail);
            }
            result.output = emitted;
        } else {
            result.output = emitted;
        }
        return failure;
    }
    LocalRuntime::GenerationStats stats;
    auto retain_partial = [&] {
        result.output = stats.output_text;
        result.prompt_tokens += stats.prompt_tokens;
        result.output_tokens += stats.output_tokens;
    };
    try {
        result.output = runtime_.translate_with_stats(prompt, generation, on_delta, should_stop, stats);
    } catch (const runtime_detail::TranslationCancelled &) {
        retain_partial();
        return {FailureCode::Cancelled, "generation cancelled"};
    } catch (const std::exception &error) {
        retain_partial();
        return {FailureCode::Runtime, error.what()};
    } catch (...) {
        retain_partial();
        return {FailureCode::Runtime, "model generation failed"};
    }
    result.prompt_tokens += stats.prompt_tokens;
    result.output_tokens += stats.output_tokens;
    result.reached_length = stats.reached_limit;
    return {};
}

Failure ModelRuntime::execute(const InvocationInput &input,
                              const SamplingOptions &sampling,
                              const RuntimeDeltaSink &on_delta,
                              const RuntimeStopPredicate &should_stop,
                              RuntimeExecution &result) {
    if (!loaded_) return {FailureCode::NotLoaded, "model is not loaded"};
    std::string prompt;
    Failure render_failure = profile_.render(input, prompt);
    if (render_failure) return render_failure;
    const int effective_output = sampling.max_output_tokens == 0 ? profile_.default_output_tokens : sampling.max_output_tokens;
    if (effective_output <= 0 || effective_output >= profile_.context_tokens)
        return {FailureCode::ContextLimit, "requested output budget leaves no prompt capacity"};
    runtime_detail::GenerationOptions generation;
    generation.context_tokens = profile_.context_tokens;
    generation.max_output_tokens = effective_output;
    generation.temperature = sampling.temperature;
    generation.top_p = sampling.top_p;
    generation.seed = sampling.seed;
    const int budget = profile_.context_tokens - effective_output;
    if (const auto *translation = std::get_if<TranslationInput>(&input)) {
        const int prompt_tokens = injection_ ? static_cast<int>(prompt.size()) : runtime_.count_prompt_tokens(prompt);
        if (prompt_tokens + effective_output <= profile_.context_tokens)
            return execute_prompt(prompt, generation, on_delta, should_stop, result);
        if (translation->overflow == OverflowPolicy::Reject)
            return {FailureCode::ContextLimit, "translation exceeds model context"};
        std::vector<std::string> chunks;
        try {
            chunks = chunk_by_token_budget(translation->text, budget, [&](const std::string &part) {
                TranslationInput candidate = *translation;
                candidate.text = part;
                std::string candidate_prompt;
                Failure failure = profile_.render(candidate, candidate_prompt);
                if (failure) throw std::runtime_error(failure.message);
                return injection_ ? static_cast<int>(candidate_prompt.size()) : runtime_.count_prompt_tokens(candidate_prompt);
            });
        } catch (const std::exception &error) {
            return {FailureCode::ContextLimit, error.what()};
        }
        if (chunks.empty()) return {FailureCode::ContextLimit, "translation could not be split to fit context"};
        result.output.clear();
        for (const std::string &chunk : chunks) {
            TranslationInput candidate = *translation;
            candidate.text = chunk;
            Failure failure = profile_.render(candidate, prompt);
            if (failure) return failure;
            RuntimeExecution part;
            failure = execute_prompt(prompt, generation, on_delta, should_stop, part);
            result.output += part.output;
            result.prompt_tokens += part.prompt_tokens;
            result.output_tokens += part.output_tokens;
            result.reached_length = result.reached_length || part.reached_length;
            if (failure) return failure;
        }
        return {};
    }
    if ((injection_ ? static_cast<int>(prompt.size()) : runtime_.count_prompt_tokens(prompt)) + effective_output > profile_.context_tokens)
        return {FailureCode::ContextLimit, "conversation exceeds model context"};
    return execute_prompt(prompt, generation, on_delta, should_stop, result);
}

}  // namespace qtrans::core::host_detail
