#include "../clipboard_capture.h"

#include <Carbon/Carbon.h>
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

    CGEventRef cDown = CGEventCreateKeyboardEvent(source, kVK_ANSI_C, true);
    CGEventRef cUp = CGEventCreateKeyboardEvent(source, kVK_ANSI_C, false);

    if (!cDown || !cUp) {
        fprintf(stderr, "[ClipboardCapture] CGEventCreate failed\n");
        if (cDown) CFRelease(cDown);
        if (cUp) CFRelease(cUp);
        CFRelease(source);
        return;
    }

    CGEventSetFlags(cDown, kCGEventFlagMaskCommand);
    CGEventSetFlags(cUp, kCGEventFlagMaskCommand);

    CGEventPost(kCGHIDEventTap, cDown);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CGEventPost(kCGHIDEventTap, cUp);

    CFRelease(cUp);
    CFRelease(cDown);
    CFRelease(source);
}

}  // namespace ClipboardCapture
