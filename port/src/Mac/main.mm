// Entry point for the native macOS build. Creates the window + MTKView that
// the game's platform layer will render into.
#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>

#import "MetalRenderer.h"

#include <pthread.h>
#include <unistd.h>

// Decompiled game entry point (Speed/Indep/Src/Misc/Main.cpp), renamed for the port.
extern int NFSMW_GameMain(int argc, char **argv);
// bWare main memory pool. The original platform startup created it before
// InitializeEverything(), which assumes pool 0 already exists.
extern void bMemoryInit();
// Game config switch (Misc/Config.cpp). Audio has no native backend yet.
extern int IsSoundEnabled;
extern int IsMemcardEnabled; // save system: the decomp's layer is GameCube-specific
// Keyboard events -> game input (InputMac.mm).
extern void PortInputInstallKeyboardMonitor();

static int g_argc;
static char **g_argv;

static void *GameThread(void *) {
    pthread_setname_np("NFSMW game");
    if (getenv("NFSMW_SOUND") == nullptr) {
        IsSoundEnabled = 0; // the game also turns off streaming, speech and NIS audio
    }
    if (getenv("NFSMW_MEMCARD") == nullptr) {
        IsMemcardEnabled = 0; // no saves until the PC profile layer is ported
    }
    bMemoryInit();
    int rc = NFSMW_GameMain(g_argc, g_argv);
    NSLog(@"NFSMW: game main returned %d", rc);
    dispatch_async(dispatch_get_main_queue(), ^{
      [NSApp terminate:nil];
    });
    return nullptr;
}

// Game data dir: --data <dir> or $NFSMW_DATA. The game uses relative paths
// ("GLOBAL\\GLOBALB.BUN"), so it runs with this as its working directory.
static NSString *GameDataDir() {
    for (int i = 1; i + 1 < g_argc; i++) {
        if (strcmp(g_argv[i], "--data") == 0) {
            return [NSString stringWithUTF8String:g_argv[i + 1]];
        }
    }
    const char *env = getenv("NFSMW_DATA");
    return env ? [NSString stringWithUTF8String:env] : nil;
}

static void StartGameThread() {
    NSString *dir = GameDataDir();
    if (!dir) {
        NSLog(@"NFSMW: no game data dir (pass --data <dir> or set NFSMW_DATA); showing renderer only");
        return;
    }
    if (chdir(dir.fileSystemRepresentation) != 0) {
        NSLog(@"NFSMW: can't chdir to %@", dir);
        return;
    }
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 64 * 1024 * 1024); // console code assumes a big stack
    pthread_t thread;
    pthread_create(&thread, &attr, GameThread, nullptr);
    pthread_attr_destroy(&attr);
}

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

    PortInputInstallKeyboardMonitor();
    StartGameThread();
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)app {
    return YES;
}

@end

int main(int argc, const char *argv[]) {
    g_argc = argc;
    g_argv = const_cast<char **>(argv);
    @autoreleasepool {
        // No window state restoration: after a crash or a killed run, AppKit
        // would otherwise block launch with a modal "reopen windows?" prompt.
        [[NSUserDefaults standardUserDefaults] setBool:YES forKey:@"ApplePersistenceIgnoreState"];
        NSApplication *app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        AppDelegate *delegate = [[AppDelegate alloc] init];
        app.delegate = delegate;
        [app activateIgnoringOtherApps:YES];
        [app run];
    }
    return 0;
}
