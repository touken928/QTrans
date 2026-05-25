#include "platform_utils.h"

#include <objc/runtime.h>
#include <objc/message.h>

namespace {

using ObjcId = id;
using MsgSend_id_id_SEL = ObjcId (*)(ObjcId, SEL);
using MsgSend_id_Class_SEL = ObjcId (*)(Class, SEL);
using MsgSend_void_id_SEL_long = void (*)(ObjcId, SEL, long);

ObjcId savedFrontApp = nil;

void macRetain(ObjcId obj) {
    if (!obj) return;
    ((MsgSend_id_id_SEL)objc_msgSend)(obj, sel_getUid("retain"));
}

void macRelease(ObjcId obj) {
    if (!obj) return;
    ((MsgSend_id_id_SEL)objc_msgSend)(obj, sel_getUid("release"));
}

} // namespace

void macSaveFrontApp() {
    savedFrontApp = nil;
    Class wsClass = objc_getClass("NSWorkspace");
    if (!wsClass) return;

    ObjcId workspace = ((MsgSend_id_Class_SEL)objc_msgSend)(wsClass, sel_getUid("sharedWorkspace"));
    if (!workspace) return;

    savedFrontApp = ((MsgSend_id_id_SEL)objc_msgSend)(workspace, sel_getUid("frontmostApplication"));
    macRetain(savedFrontApp);
}

void macRestoreFrontApp() {
    if (!savedFrontApp) return;

    ((MsgSend_void_id_SEL_long)objc_msgSend)(
        savedFrontApp, sel_getUid("activateWithOptions:"), (long)1);
    macRelease(savedFrontApp);
    savedFrontApp = nil;
}
