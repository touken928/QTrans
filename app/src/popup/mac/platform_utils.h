#pragma once

#ifdef __APPLE__

void macSaveFrontApp();
void macRestoreFrontApp();
// Re-activates the app that was frontmost when macSaveFrontApp() ran, without
// consuming the saved reference (unlike macRestoreFrontApp). Used to re-assert
// the user's front app after the popup show, which can activate QTrans
// asynchronously and pull the Space away under Stage Manager.
void macReassertFrontApp();
bool macEnsureAccessibilityTrusted(bool prompt);

#endif
