// Entry point for the native macOS build. Creates the window + MTKView that
// the game's platform layer will render into.
#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>

#import "MetalRenderer.h"

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AppDelegate {
    NSWindow *_window;
    MetalRenderer *_renderer;
}

- (void)applicationDidFinishLaunching:(NSNotification *)note {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        NSLog(@"NFSMW: no Metal device available");
        [NSApp terminate:nil];
        return;
    }

    NSRect frame = NSMakeRect(0, 0, 1280, 720);
    _window = [[NSWindow alloc] initWithContentRect:frame
                                          styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                    NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    _window.title = @"Need for Speed: Most Wanted";

    MTKView *view = [[MTKView alloc] initWithFrame:frame device:device];
    _renderer = [[MetalRenderer alloc] initWithView:view];
    view.delegate = _renderer;

    _window.contentView = view;
    [_window center];
    [_window makeKeyAndOrderFront:nil];
    NSLog(@"NFSMW: Metal device %@", device.name);
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app {
    return YES;
}

@end

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        AppDelegate *delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app activateIgnoringOtherApps:YES];
        [app run];
    }
    return 0;
}
