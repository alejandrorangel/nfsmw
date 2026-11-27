// Entry point for the native macOS build. Creates the window + MTKView that
// the game's platform layer will render into.
#import <Cocoa/Cocoa.h>
#import <MetalKit/MetalKit.h>

#import "MetalRenderer.h"
#include "MetalBridge.h"
#include "../Platform/Benchmark.h"

#include <pthread.h>
#include <unistd.h>

// Decompiled game entry point (Speed/Indep/Src/Misc/Main.cpp), renamed for the port.
extern int NFSMW_GameMain(int argc, char **argv);
// bWare main memory pool. The original platform startup created it before
// InitializeEverything(), which assumes pool 0 already exists.
extern void bMemoryInit();
// Game config switch (Misc/Config.cpp). Sound is still opt-in (NFSMW_SOUND=1):
// frontend sound effects play natively, music and in-game audio don't yet.
extern int IsSoundEnabled;
extern int IsMemcardEnabled; // profiles (the PC's file-based realmc store)
extern bool UnlockAllThings;
// Keyboard events -> game input (InputMac.mm).
#include "../Audio/AudioMixer.h"

extern void PortInputInstallKeyboardMonitor();

static int g_argc;
static char **g_argv;

static void *GameThread(void *) {
    pthread_setname_np("NFSMW game");
    if (getenv("NFSMW_SOUND") == nullptr) {
        IsSoundEnabled = 0; // the game also turns off streaming, speech and NIS audio
    }
    // Profiles are on, as on PC; NFSMW_MEMCARD=0 plays without them (no saves).
    if (const char *memcard = getenv("NFSMW_MEMCARD")) {
        if (strcmp(memcard, "0") == 0) {
            IsMemcardEnabled = 0;
        }
    }
    // NFSMW_UNLOCK_ALL=1: the game's own "unlock all things" switch (cars,
    // parts, vinyls, ...) for testing customization without a career save.
    if (const char *unlock = getenv("NFSMW_UNLOCK_ALL")) {
        UnlockAllThings = strcmp(unlock, "0") != 0;
    }
    PortBenchmarkInit(); // NFSMW_BENCHMARK: the scenario's race switches, no sound
    bMemoryInit();
    if (IsSoundEnabled != 0) {
        port_audio::StartAudioOutput();
    }
    if (getenv("NFSMW_TEST_AUDIO") != nullptr && port_audio::StartAudioOutput()) {
        port_audio::RunAudioSelfTest();
    }
    int rc = NFSMW_GameMain(g_argc, g_argv);
    NSLog(@"NFSMW: game main returned %d", rc);
    // The game has shut itself down (Quit). Leave without running static
    // destructors: the decomp's global containers aren't safe to destroy at
    // exit (crash in bTList<PresetCar>::~bTList from [NSApp terminate:]).
    port_audio::StopAudioOutput();
    fflush(nullptr);
    _exit(rc);
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

// The game window and the main screen's usable size (points), for the PC
// display mode (graphics options; GraphicsOptionsNative.cpp).
static NSWindow *gWindow;
static MTKView *gView;
static NSSize gScreenSize;

int PortDisplayModeFits(int width, int height) {
    return width <= gScreenSize.width && height <= gScreenSize.height;
}

void PortSetDisplayMode(int width, int height, bool vsync) {
    vsync = PortBenchmarkVSync(vsync);
    dispatch_async(dispatch_get_main_queue(), ^{
      if (gView) {
          ((CAMetalLayer *)gView.layer).displaySyncEnabled = vsync;
      }
      if (gWindow && width > 0 && height > 0 && (gWindow.styleMask & NSWindowStyleMaskFullScreen) == 0) {
          NSSize size = NSMakeSize(width, height);
          if (!NSEqualSizes(gWindow.contentView.frame.size, size)) {
              [gWindow setContentSize:size];
              [gWindow center];
          }
      }
    });
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
    gWindow = _window;
    gView = view;
    if (PortBenchmarkActive()) {
        PortMacBenchmarkSetupView((__bridge void *)view); // the game thread presents each frame
    }
    gScreenSize = NSScreen.mainScreen ? NSScreen.mainScreen.visibleFrame.size : NSMakeSize(1280, 800);
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
    PortBenchmarkConfigure(argc, g_argv);
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
