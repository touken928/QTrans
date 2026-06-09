#pragma once

namespace qtrans::log {

enum class Component {
    App,
    Hymt,
    Inference,
    Task,
    Download,
    WordSelect,
    Clipboard,
};

const char *component_name(Component component);

}  // namespace qtrans::log
