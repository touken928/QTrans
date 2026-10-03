#pragma once

#include "../local_runtime.h"
#include "prompt_profiles.h"

#include <functional>
#include <string>

namespace qtrans::core::host_detail {

struct RuntimeExecution {
    std::string output;
    int prompt_tokens = 0;
    int output_tokens = 0;
    bool reached_length = false;
};

using RuntimeDeltaSink = std::function<void(std::string_view)>;
using RuntimeStopPredicate = std::function<bool()>;
using RuntimeInjection = std::function<Failure(const std::string &,
                                               const runtime_detail::GenerationOptions &,
                                               const RuntimeDeltaSink &,
                                               const RuntimeStopPredicate &,
                                               RuntimeExecution &)>;

class ModelRuntime {
public:
    explicit ModelRuntime(RuntimeInjection injection = {});

    Failure load(const ModelSpec &model);
    Failure unload() noexcept;
    [[nodiscard]] bool loaded() const noexcept;
    [[nodiscard]] const PromptProfile &profile() const noexcept;

    Failure execute(const InvocationInput &input,
                    const SamplingOptions &sampling,
                    const RuntimeDeltaSink &on_delta,
                    const RuntimeStopPredicate &should_stop,
                    RuntimeExecution &result);

private:
    Failure execute_prompt(const std::string &prompt,
                           const runtime_detail::GenerationOptions &generation,
                           const RuntimeDeltaSink &on_delta,
                           const RuntimeStopPredicate &should_stop,
                           RuntimeExecution &result);

    LocalRuntime runtime_;
    PromptProfile profile_{};
    bool loaded_ = false;
    RuntimeInjection injection_;
};

}  // namespace qtrans::core::host_detail
