#include "../clipboard_capture.h"

#include <windows.h>
#include <chrono>
#include <thread>

namespace ClipboardCapture {

void simulateCopy() {
    ::keybd_event(VK_CONTROL, 0, 0, 0);
    ::keybd_event('C', 0, 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ::keybd_event('C', 0, KEYEVENTF_KEYUP, 0);
    ::keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
}

}  // namespace ClipboardCapture
