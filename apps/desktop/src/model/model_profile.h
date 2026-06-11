#pragma once

#include <string>

class ModelProfile {
public:
    virtual ~ModelProfile() = default;

    virtual const char *id() const = 0;
    virtual const char *display_name() const = 0;
    virtual const char *filename() const = 0;
    virtual const char *remote_spec() const = 0;
    virtual const char *modelscope_remote_spec() const = 0;
    virtual int download_hub() const {
        return 2;
    }
};

const ModelProfile &hymt18b_q4_profile();
const ModelProfile &hymt7b_q4_profile();
