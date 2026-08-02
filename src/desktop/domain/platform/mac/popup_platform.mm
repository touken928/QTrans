#import "domain/platform/mac/popup_platform.h"

#import <AppKit/AppKit.h>

void macConfigurePopupWindow(void *nativeView) {
    if (nativeView == nullptr) {
        return;
    }
    NSView *view = (NSView *)nativeView;
    NSWindow *window = view.window;
    if (window == nil) {
        return;
    }
    // Order matters: the non-activating panel style mask must be applied
    // before the collection behavior, otherwise the auxiliary behavior is
    // dropped when the mask is set. With the mask in place, ordering the popup
    // front never activates the QTrans app, so Stage Manager never pulls the
    // Space over to the QTrans desktop.
    if ([window isKindOfClass:[NSPanel class]]) {
        NSPanel *panel = (NSPanel *)window;
        panel.styleMask |= NSWindowStyleMaskNonactivatingPanel;
        panel.floatingPanel = YES;
        panel.hidesOnDeactivate = NO;
    }
    // MoveToActiveSpace presents the popup on the currently active Space
    // instead of switching the user to the Space the app's windows are on
    // (CanJoinAllSpaces only makes it visible everywhere and does NOT prevent
    // that switch). FullScreenAuxiliary keeps it usable over full-screen apps.
    window.collectionBehavior =
        NSWindowCollectionBehaviorMoveToActiveSpace |
        NSWindowCollectionBehaviorFullScreenAuxiliary |
        NSWindowCollectionBehaviorIgnoresCycle;
    [window setLevel:NSPopUpMenuWindowLevel];
}
