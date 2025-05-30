// AppDelegate.mm

//#import "AppDelegate.h"
#import <Cocoa/Cocoa.h>
#import <QtCore/QMetaObject.h>
#import "NextAppCore.h"

// Tell the compiler what AppDelegate is, and that it implements NSApplicationDelegate
@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AppDelegate

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    // existing setup…

    [[[NSWorkspace sharedWorkspace] notificationCenter]
        addObserver:self
           selector:@selector(receiveWakeNote:)
               name:NSWorkspaceDidWakeNotification
             object:nil];
}

- (void)receiveWakeNote:(NSNotification*)note {
    // notify your Qt object
    QMetaObject::invokeMethod(NextAppCore::instance(), "onWokeFromSleep", Qt::QueuedConnection);
}

@end
