#import <AppKit/AppKit.h>

#include <unistd.h>

void activateExistingApplication() {
    @autoreleasepool {
        const pid_t self_pid = getpid();
        for (NSRunningApplication *app in NSWorkspace.sharedWorkspace.runningApplications) {
            if (app.processIdentifier == self_pid) {
                continue;
            }
            if (![app.localizedName isEqualToString:@"QTrans"]) {
                continue;
            }

            [app activateWithOptions:NSApplicationActivateAllWindows];
            break;
        }
    }
}
