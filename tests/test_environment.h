#pragma once

#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace test_support {

struct EnvironmentValue {
    std::string value;
    bool present = false;
};

inline EnvironmentValue read_environment(const char *name) {
    if (const char *value = std::getenv(name); value != nullptr) {
        return {value, true};
    }
    return {};
}

inline void set_environment(const char *name, const std::string &value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    ::setenv(name, value.c_str(), 1);
#endif
}

inline void unset_environment(const char *name) {
#ifdef _WIN32
    _putenv_s(name, "");
#else
    ::unsetenv(name);
#endif
}

inline void restore_environment(const char *name,
                                const EnvironmentValue &saved) {
    if (saved.present) {
        set_environment(name, saved.value);
    } else {
        unset_environment(name);
    }
}

inline int current_pid() {
#ifdef _WIN32
    return ::_getpid();
#else
    return ::getpid();
#endif
}

}  // namespace test_support
