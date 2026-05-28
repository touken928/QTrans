#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

BOOL CALLBACK findMainWindow(HWND hwnd, LPARAM) {
    wchar_t title[256] = {};
    if (GetWindowTextW(hwnd, title, 256) == 0) {
        return TRUE;
    }

    if (wcscmp(title, L"QTrans") != 0) {
        return TRUE;
    }

    if (!IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_SHOW);
    }
    ShowWindow(hwnd, SW_RESTORE);
    SetForegroundWindow(hwnd);
    return FALSE;
}

} // namespace

void activateExistingApplication() {
    EnumWindows(findMainWindow, 0);
}
