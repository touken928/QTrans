#include "runtime/default_runtime_factory.h"

#include "runtime/local_runtime.h"
#include "runtime/remote_runtime.h"

#include <stdexcept>

namespace qtrans::core {

DefaultRuntimeFactory::DefaultRuntimeFactory(BackendOptions backend_opts)
    : backend_opts_(std::move(backend_opts)) {
}

std::unique_ptr<ITranslationRuntime> DefaultRuntimeFactory::create_runtime(
    const ModelLoadSpec &model) {
    if (std::holds_alternative<LocalModelConfig>(model)) {
        auto rt = std::make_unique<LocalRuntime>();
        rt->initialize_backend(backend_opts_);
        return rt;
    }
    return std::make_unique<RemoteRuntime>();
}

}  // namespace qtrans::core
