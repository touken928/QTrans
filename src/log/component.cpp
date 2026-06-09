#include "log/component.h"

namespace qtrans::log {

const char *component_name(Component component) {
    switch (component) {
        case Component::App:
            return "app";
        case Component::Hymt:
            return "hymt";
        case Component::Inference:
            return "inference";
        case Component::Task:
            return "task";
        case Component::Download:
            return "download";
        case Component::WordSelect:
            return "wordselect";
        case Component::Clipboard:
            return "clipboard";
    }
    return "unknown";
}

}  // namespace qtrans::log
