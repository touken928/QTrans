#include "popup/popup_surface.h"
#include "popup/popup_escape_queue.h"

#include <windows.h>

namespace {

HHOOK g_escape_hook = nullptr;
QObject *g_escape_hook_owner = nullptr;
void (*g_escape_hook_on_escape)(QObject *) = nullptr;
bool (*g_escape_hook_modal_consumes_escape)() = nullptr;

LRESULT CALLBACK escapeHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN &&
        !(g_escape_hook_modal_consumes_escape != nullptr &&
          g_escape_hook_modal_consumes_escape())) {
        const auto *kb = reinterpret_cast<const KBDLLHOOKSTRUCT *>(lParam);
        if (kb->vkCode == VK_ESCAPE) {
            if (QObject *owner = g_escape_hook_owner) {
                // Deferred: never mutate Qt state from inside the hook.
                queuePopupEscape(owner, g_escape_hook_on_escape);
            }
            // Swallow: while the popup is visible, Escape belongs to it.
            return 1;
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

}  // namespace

void configurePopupWindow(void * /*native_view*/) {
}

void saveFrontApp() {
}

void restoreFrontApp() {
}

void reassertFrontApp() {
}

bool ensureAccessibilityTrusted(bool /*prompt*/) {
    return true;
}

void installPopupEscapeMonitor(QObject *owner,
                               void (*on_escape)(QObject *owner),
                               bool (*modal_consumes_escape)()) {
    if (g_escape_hook == nullptr) {
        g_escape_hook = SetWindowsHookExW(WH_KEYBOARD_LL, &escapeHookProc,
                                          GetModuleHandleW(nullptr), 0);
        if (g_escape_hook != nullptr) {
            g_escape_hook_owner = owner;
            g_escape_hook_on_escape = on_escape;
            g_escape_hook_modal_consumes_escape = modal_consumes_escape;
        }
    }
}

void uninstallPopupEscapeMonitor(QObject *owner) {
    if (g_escape_hook_owner == owner) {
        g_escape_hook_owner = nullptr;
        g_escape_hook_on_escape = nullptr;
        g_escape_hook_modal_consumes_escape = nullptr;
        if (g_escape_hook != nullptr) {
            UnhookWindowsHookEx(g_escape_hook);
            g_escape_hook = nullptr;
        }
    }
}
