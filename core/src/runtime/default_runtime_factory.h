#pragma once

#include "qtrans/core/runtime.h"

namespace qtrans::core {

class DefaultRuntimeFactory : public ITranslationRuntimeFactory {
public:
    explicit DefaultRuntimeFactory(BackendOptions backend_opts);
    std::unique_ptr<ITranslationRuntime> create_runtime(
        const ModelLoadSpec &model) override;

private:
    BackendOptions backend_opts_;
};

}  // namespace qtrans::core
