#pragma once

#include <cstdint>

namespace qtrans::log {

class ConsoleProgress {
public:
    void set_enabled(bool enabled);
    void update(std::int64_t percent);
    void finish();

private:
    bool enabled_ = true;
    std::int64_t last_reported_ = -1;
};

}  // namespace qtrans::log
