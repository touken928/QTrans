#include "log/console_progress.h"

#include <cstdio>

namespace qtrans::log {

void ConsoleProgress::set_enabled(bool enabled) {
    enabled_ = enabled;
}

void ConsoleProgress::update(std::int64_t percent) {
    if (!enabled_) {
        return;
    }
    if (percent == last_reported_) {
        return;
    }
    last_reported_ = percent;
    std::fprintf(stderr, "\rdownloading: %3lld%%", static_cast<long long>(percent));
    std::fflush(stderr);
}

void ConsoleProgress::finish() {
    if (!enabled_ || last_reported_ < 0) {
        return;
    }
    std::fprintf(stderr, "\n");
    last_reported_ = -1;
}

}  // namespace qtrans::log
