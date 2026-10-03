#import "popup/popup_surface.h"

#import "popup/mac/platform_utils.h"
#import "popup/mac/popup_platform.h"

#import "popup/popup_escape_queue.h"

#import <ApplicationServices/ApplicationServices.h>
#import <Carbon/Carbon.h>

namespace {

CFMachPortRef g_escape_tap_port = nullptr;
CFRunLoopSourceRef g_escape_tap_source = nullptr;
QObject *g_escape_tap_owner = nullptr;
void (*g_escape_tap_on_escape)(QObject *) = nullptr;
bool (*g_escape_tap_modal_consumes_escape)() = nullptr;

CGEventRef escapeTapCallback(CGEventTapProxy proxy, CGEventType type,
                             CGEventRef event, void *info) {
    (void)proxy;
    (void)info;
    if (type == kCGEventTapDisabledByTimeout ||
        type == kCGEventTapDisabledByUserInput) {
        // Re-enable as long as the popup still owns the tap.
        if (g_escape_tap_port != nullptr && g_escape_tap_owner != nullptr) {
            CGEventTapEnable(g_escape_tap_port, true);
        }
        return event;
    }
    if (type == kCGEventKeyDown &&
        !(g_escape_tap_modal_consumes_escape != nullptr &&
          g_escape_tap_modal_consumes_escape()) &&
        CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode) == kVK_Escape) {
        if (QObject *owner = g_escape_tap_owner) {
            // Deferred: never mutate Qt state from inside the tap callback.
            queuePopupEscape(owner, g_escape_tap_on_escape);
        }
        // Swallow: while the popup is visible, Escape belongs to it.
        return nullptr;
    }
    return event;
}

}  // namespace

void configurePopupWindow(void *native_view) {
    macConfigurePopupWindow(native_view);
}

void saveFrontApp() {
    macSaveFrontApp();
}

void restoreFrontApp() {
    macRestoreFrontApp();
}

void reassertFrontApp() {
    macReassertFrontApp();
}

bool ensureAccessibilityTrusted(bool prompt) {
    return macEnsureAccessibilityTrusted(prompt);
}

void installPopupEscapeMonitor(QObject *owner,
                               void (*on_escape)(QObject *owner),
                               bool (*modal_consumes_escape)()) {
    // The global tap needs the accessibility trust word-select already
    // requires; without it we fall back to the Qt-level filter only.
    if (AXIsProcessTrusted() && g_escape_tap_port == nullptr) {
        g_escape_tap_port = CGEventTapCreate(
            kCGHIDEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
            CGEventMaskBit(kCGEventKeyDown), &escapeTapCallback, nullptr);
        if (g_escape_tap_port != nullptr) {
            g_escape_tap_source = CFMachPortCreateRunLoopSource(
                kCFAllocatorDefault, g_escape_tap_port, 0);
            if (g_escape_tap_source != nullptr) {
                CFRunLoopAddSource(CFRunLoopGetMain(), g_escape_tap_source,
                                   kCFRunLoopCommonModes);
            }
            CGEventTapEnable(g_escape_tap_port, true);
            g_escape_tap_owner = owner;
            g_escape_tap_on_escape = on_escape;
            g_escape_tap_modal_consumes_escape = modal_consumes_escape;
        }
    }
}

void uninstallPopupEscapeMonitor(QObject *owner) {
    if (g_escape_tap_owner == owner) {
        g_escape_tap_owner = nullptr;
        g_escape_tap_on_escape = nullptr;
        g_escape_tap_modal_consumes_escape = nullptr;
        if (g_escape_tap_source != nullptr) {
            CFRunLoopRemoveSource(CFRunLoopGetMain(), g_escape_tap_source,
                                  kCFRunLoopCommonModes);
            CFRelease(g_escape_tap_source);
            g_escape_tap_source = nullptr;
        }
        if (g_escape_tap_port != nullptr) {
            CGEventTapEnable(g_escape_tap_port, false);
            CFRelease(g_escape_tap_port);
            g_escape_tap_port = nullptr;
        }
    }
}
