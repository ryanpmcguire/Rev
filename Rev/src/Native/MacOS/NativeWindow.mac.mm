#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#import "NativeWindow.mac.h"

@interface RevMacWindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) void* userData;
@property (nonatomic, assign) RevMacEventAcceptor acceptor;
@end

// Window delegate
//--------------------------------------------------

@implementation RevMacWindowDelegate
- (void)windowDidResize:(NSNotification*)n {
    NSWindow* win = (NSWindow*)n.object;
    NSRect f = [win frame];
    WinEvent ev { WinEvent::Resize /*Resize*/, 0, 0, (int)f.size.width, (int)f.size.height };
    if (self.acceptor) self.acceptor(self.userData, ev);
}
- (void)windowWillClose:(NSNotification*)n {
    WinEvent ev { WinEvent::Destroy /*Destroy*/, 0, 0, 0, 0 };
    if (self.acceptor) self.acceptor(self.userData, ev);
}
- (void)windowDidBecomeKey:(NSNotification*)n {
    WinEvent ev { WinEvent::Focus /*Focus*/, 0, 0, 0, 0 };
    if (self.acceptor) self.acceptor(self.userData, ev);
}
- (void)windowDidResignKey:(NSNotification*)n {
    WinEvent ev { WinEvent::Defocus /*Defocus*/, 0, 0, 0, 0 };
    if (self.acceptor) self.acceptor(self.userData, ev);
}
@end

// View
//--------------------------------------------------

@interface RevView : NSView
@property (nonatomic, assign) void* userData;
@property (nonatomic, assign) RevMacEventAcceptor acceptor;
@end

@implementation RevView
- (BOOL)acceptsFirstResponder { return YES; }
- (void)mouseDown:(NSEvent *)e {
    NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
    WinEvent ev { WinEvent::MouseButton /*MouseButton*/, 0, 1, (int)p.x, (int)p.y };
    if (self.acceptor) self.acceptor(self.userData, ev);
}
- (void)mouseUp:(NSEvent *)e {
    NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
    WinEvent ev { WinEvent::MouseButton /*MouseButton*/, 0, 0, (int)p.x, (int)p.y };
    if (self.acceptor) self.acceptor(self.userData, ev);
}
- (void)mouseMoved:(NSEvent *)e {
    NSPoint p = [self convertPoint:[e locationInWindow] fromView:nil];
    WinEvent ev { WinEvent::MouseMove /*MouseMove*/, 0, 0, (int)p.x, (int)p.y };
    if (self.acceptor) self.acceptor(self.userData, ev);
}
- (void)keyDown:(NSEvent *)e {
    WinEvent ev { WinEvent::Keyboard /*Keyboard*/, (uint64_t)[e keyCode], 1, 0, 0 };
    if (self.acceptor) self.acceptor(self.userData, ev);
}
- (void)keyUp:(NSEvent *)e {
    WinEvent ev { WinEvent::Keyboard /*Keyboard*/, (uint64_t)[e keyCode], 0, 0, 0 };
    if (self.acceptor) self.acceptor(self.userData, ev);
}
- (void)insertText:(id)string {
    NSString* s = (NSString*)string;
    for (NSUInteger i = 0; i < [s length]; ++i) {
        unichar ch = [s characterAtIndex:i];
        WinEvent ev { WinEvent::Character /*Character*/, (uint64_t)ch, 0, 0, 0 };
        if (self.acceptor) self.acceptor(self.userData, ev);
    }
}
@end

// Window creation / management
//--------------------------------------------------

RevMacWindowHandle rev_mac_window_create(int width, int height,
                                         void* userData,
                                         RevMacEventAcceptor acceptor,
                                         void* parent) // extra arg
{
    @autoreleasepool {
        NSRect frame = NSMakeRect(100,100,width,height);

        RevView* view = [[RevView alloc] initWithFrame:frame];
        view.userData = userData;
        view.acceptor = acceptor;

        if (parent) {
            // Parent is an NSView* provided by host (e.g. VST)
            NSView* parentView = (__bridge NSView*)parent;
            [parentView addSubview:view];
            return (__bridge_retained void*)view; // return the view handle
        } else {
            // No parent → create our own window
            NSWindow* window = [[NSWindow alloc]
                initWithContentRect:frame
                          styleMask:(NSWindowStyleMaskTitled |
                                     NSWindowStyleMaskClosable |
                                     NSWindowStyleMaskResizable)
                            backing:NSBackingStoreBuffered
                              defer:NO];

            RevMacWindowDelegate* delegate = [[RevMacWindowDelegate alloc] init];
            delegate.userData = userData;
            delegate.acceptor = acceptor;
            [window setDelegate:delegate];
            [window setContentView:view];

            objc_setAssociatedObject(window, "rev_delegate", delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            objc_setAssociatedObject(window, "rev_view", view, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

            [window makeKeyAndOrderFront:nil];
            return (__bridge_retained void*)window; // return the window handle
        }
    }
}


void rev_mac_window_destroy(RevMacWindowHandle handle) {
    if (!handle) return;
    NSWindow* window = (__bridge_transfer NSWindow*)handle;
    [window close];
}

void rev_mac_window_set_size(RevMacWindowHandle handle, int w, int h) {
    if (!handle) return;
    NSWindow* window = (__bridge NSWindow*)handle;
    NSRect frame = [window frame];
    frame.size = NSMakeSize(w,h);
    [window setFrame:frame display:YES];
}

// Utility
//--------------------------------------------------

void rev_mac_wait_event() {
    @autoreleasepool {
        // Block until the next event
        NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate distantFuture]  // blocking
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
        if (event) {
            [NSApp sendEvent:event];
            [NSApp updateWindows];
        }
    }
}
