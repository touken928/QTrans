#include "../clipboard_capture.h"

#include <CoreGraphics/CoreGraphics.h>
#include <chrono>
#include <cstdio>
#include <thread>

namespace ClipboardCapture {

void simulateCopy() {
    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    if (!source) {
        fprintf(stderr, "[ClipboardCapture] CGEventSourceCreate failed\n");
        return;
    }

    CGEventRef cmdDown = CGEventCreateKeyboardEvent(source, (CGKeyCode)55, true);
    CGEventRef cDown   = CGEventCreateKeyboardEvent(source, (CGKeyCode)8, true);
    CGEventRef cUp     = CGEventCreateKeyboardEvent(source, (CGKeyCode)8, false);
    CGEventRef cmdUp   = CGEventCreateKeyboardEvent(source, (CGKeyCode)55, false);

    if (!cmdDown || !cDown || !cUp || !cmdUp) {
        fprintf(stderr, "[ClipboardCapture] CGEventCreate failed\n");
        if (cmdDown) CFRelease(cmdDown);
        if (cDown) CFRelease(cDown);
        if (cUp) CFRelease(cUp);
        if (cmdUp) CFRelease(cmdUp);
        CFRelease(source);
        return;
    }

    CGEventPost(kCGHIDEventTap, cmdDown);
    CGEventPost(kCGHIDEventTap, cDown);
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    CGEventPost(kCGHIDEventTap, cUp);
    CGEventPost(kCGHIDEventTap, cmdUp);

    CFRelease(cUp);
    CFRelease(cDown);
    CFRelease(cmdUp);
    CFRelease(cmdDown);
    CFRelease(source);
}

} // namespace ClipboardCapture
