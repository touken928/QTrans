#pragma once

#include "qtrans/core.h"

namespace qtrans::core::test {

struct TestGeneration {
    std::string output;
    int prompt_tokens = 1;
    int output_tokens = 1;
    bool reached_length = false;
    Failure failure;
};

struct ModelHostHooks {
    std::function<Failure(const ModelSpec &)> load_runtime;
    std::function<Failure()> unload_runtime;
    std::function<void()> before_invocation;
    std::function<TestGeneration(std::string_view, const SamplingOptions &,
                                 const std::function<void(std::string_view)> &,
                                 const std::function<bool()> &)>
        generate;
};

class ScopedModelHostHooks {
public:
    explicit ScopedModelHostHooks(const ModelHostHooks &hooks);
    ~ScopedModelHostHooks();

    ScopedModelHostHooks(const ScopedModelHostHooks &) = delete;
    ScopedModelHostHooks &operator=(const ScopedModelHostHooks &) = delete;

private:
    ModelHostHooks effective_;
    const ModelHostHooks *previous_ = nullptr;
};

const ModelHostHooks *active_model_host_hooks() noexcept;
ModelHostHooks default_model_host_hooks();

}  // namespace qtrans::core::test
