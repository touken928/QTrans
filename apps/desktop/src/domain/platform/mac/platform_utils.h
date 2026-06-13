#pragma once

#ifdef __APPLE__

void macSaveFrontApp();
void macRestoreFrontApp();
bool macEnsureAccessibilityTrusted(bool prompt);

#endif
