#pragma once

class SingleInstance {
public:
    static bool ensurePrimaryOrActivateExisting();
};
