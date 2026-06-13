#include "runtime/default_runtime_factory.h"

#include "runtime/local_runtime.h"
#include "runtime/remote_runtime.h"

#include <stdexcept>

namespace qtrans::core {

DefaultRuntimeFactory::DefaultRuntimeFactory(ResolvedBackendEnvironment environment,
                                             BackendOptions backend_opts)
    : environment_(std::move(environment)),
      backend_opts_(std::move(backend_opts)) {
}

std::unique_ptr<ITranslationRuntime> DefaultRuntimeFactory::create_runtime(
    const ModelLoadSpec &model) {
    if (std::holds_alternative<LocalModelConfig>(model)) {
        return std::make_unique<LocalRuntime>(environment_, backend_opts_);
    }
    return std::make_unique<RemoteRuntime>();
}

}  // namespace qtrans::core
