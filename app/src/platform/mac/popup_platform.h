#pragma once

// macOS-native window configuration for the popup. Implemented in
// popup_platform.mm (Objective-C++) so Qt C++ translation units can call it
// without becoming Objective-C++ themselves.
#ifdef __cplusplus
extern "C" {
#endif

// Presents the popup as a non-activating floating panel on the active Space:
// showing it never activates the QTrans app and never switches the user's
// Space (Stage Manager / Mission Control).
void macConfigurePopupWindow(void *nativeView);

#ifdef __cplusplus
}
#endif
