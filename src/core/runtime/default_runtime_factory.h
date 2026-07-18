#pragma once

#include "qtrans/core/backend_environment.h"
#include "qtrans/core/runtime.h"

namespace qtrans::core {

class DefaultRuntimeFactory : public ITranslationRuntimeFactory {
public:
    DefaultRuntimeFactory(ResolvedBackendEnvironment environment, BackendOptions backend_opts);
    std::unique_ptr<ITranslationRuntime> create_runtime(
        const ModelLoadSpec &model) override;

private:
    ResolvedBackendEnvironment environment_;
    BackendOptions backend_opts_;
};

}  // namespace qtrans::core
