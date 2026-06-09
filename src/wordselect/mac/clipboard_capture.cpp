#include "../clipboard_capture.h"

#include "log/component.h"
#include "log/logger.h"

#include <Carbon/Carbon.h>
#include <CoreGraphics/CoreGraphics.h>
#include <chrono>
#include <thread>

namespace ClipboardCapture {

void simulateCopy() {
    auto logger = qtrans::log::get(qtrans::log::Component::Clipboard);

    CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
    if (!source) {
        logger->error("CGEventSourceCreate failed");
        return;
    }

    CGEventRef cDown = CGEventCreateKeyboardEvent(source, kVK_ANSI_C, true);
    CGEventRef cUp = CGEventCreateKeyboardEvent(source, kVK_ANSI_C, false);

    if (!cDown || !cUp) {
        logger->error("CGEventCreate failed");
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
