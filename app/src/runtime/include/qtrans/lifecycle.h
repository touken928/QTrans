#pragma once

namespace qtrans::core {
enum class LifecycleState {
    Unloaded,
    Loading,
    Ready,
    Unloading,
    Draining,
    ShuttingDown,
    Stopped
};
}  // namespace qtrans::core
