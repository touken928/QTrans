#include "domain/platform/mac/platform_utils.h"

#include <ApplicationServices/ApplicationServices.h>
#include <objc/runtime.h>
#include <objc/message.h>

namespace {

using ObjcId = id;
using MsgSend_id_id_SEL = ObjcId (*)(ObjcId, SEL);
using MsgSend_id_Class_SEL = ObjcId (*)(Class, SEL);
using MsgSend_void_id_SEL_long = void (*)(ObjcId, SEL, long);

ObjcId savedFrontApp = nil;
// Retained copy that survives macRestoreFrontApp()'s clear, so the deferred
// front-app re-assert (after the popup show) can still reach the original app.
ObjcId lastFrontApp = nil;

void macRetain(ObjcId obj) {
    if (!obj) return;
    ((MsgSend_id_id_SEL)objc_msgSend)(obj, sel_getUid("retain"));
}

void macRelease(ObjcId obj) {
    if (!obj) return;
    ((MsgSend_id_id_SEL)objc_msgSend)(obj, sel_getUid("release"));
}

}  // namespace

void macSaveFrontApp() {
    savedFrontApp = nil;
    lastFrontApp = nil;
    Class wsClass = objc_getClass("NSWorkspace");
    if (!wsClass) return;

    ObjcId workspace = ((MsgSend_id_Class_SEL)objc_msgSend)(wsClass, sel_getUid("sharedWorkspace"));
    if (!workspace) return;

    ObjcId frontmost = ((MsgSend_id_id_SEL)objc_msgSend)(workspace, sel_getUid("frontmostApplication"));
    savedFrontApp = frontmost;
    macRetain(savedFrontApp);
    lastFrontApp = frontmost;
    macRetain(lastFrontApp);
}

void macRestoreFrontApp() {
    if (!savedFrontApp) return;

    // NSApplicationActivateIgnoringOtherApps
    ((MsgSend_void_id_SEL_long)objc_msgSend)(
        savedFrontApp, sel_getUid("activateWithOptions:"), (long)2);
    macRelease(savedFrontApp);
    savedFrontApp = nil;
}

void macReassertFrontApp() {
    if (!lastFrontApp) return;

    // NSApplicationActivateIgnoringOtherApps
    ((MsgSend_void_id_SEL_long)objc_msgSend)(
        lastFrontApp, sel_getUid("activateWithOptions:"), (long)2);
}

bool macEnsureAccessibilityTrusted(bool prompt) {
    if (AXIsProcessTrusted()) {
        return true;
    }
    if (!prompt) {
        return false;
    }

    const void *keys[] = {kAXTrustedCheckOptionPrompt};
    const void *values[] = {kCFBooleanTrue};
    CFDictionaryRef options = CFDictionaryCreate(
        kCFAllocatorDefault,
        keys,
        values,
        1,
        &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    const bool trusted = AXIsProcessTrustedWithOptions(options);
    CFRelease(options);
    return trusted;
}
