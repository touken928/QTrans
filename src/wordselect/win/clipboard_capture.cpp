#include "../clipboard_capture.h"

#include <windows.h>

#include <chrono>
#include <thread>

namespace ClipboardCapture {

void simulateCopy() {
    INPUT inputs[4] = {};

    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;

    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'C';

    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'C';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

    ::SendInput(4, inputs, sizeof(INPUT));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

}  // namespace ClipboardCapture
