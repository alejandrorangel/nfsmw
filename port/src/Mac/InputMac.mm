// Keyboard (Cocoa key events) and gamepad (GameController framework) state.

#import <Cocoa/Cocoa.h>
#import <GameController/GameController.h>

#include "InputBridge.h"
#include "MetalBridge.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

static std::mutex gInputLock;
static float gKeys[PORT_KEY_COUNT];
static float gMouseU = 0.5f, gMouseV = 0.5f; // view fraction, top-left origin
static unsigned int gMouseButtons;

static int PortKeyForKeyCode(unsigned short code) {
    // macOS virtual key codes (Carbon kVK_*).
    static const unsigned char letters[26] = {0x00, 0x0B, 0x08, 0x02, 0x0E, 0x03, 0x05, 0x04, 0x22, 0x26, 0x28, 0x25, 0x2E,
                                              0x2D, 0x1F, 0x23, 0x0C, 0x0F, 0x01, 0x11, 0x20, 0x09, 0x0D, 0x07, 0x10, 0x06};
    static const unsigned char digits[10] = {0x1D, 0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1A, 0x1C, 0x19};
    for (int i = 0; i < 26; i++) {
        if (letters[i] == code) {
            return PORT_KEY_A + i;
        }
    }
    for (int i = 0; i < 10; i++) {
        if (digits[i] == code) {
            return PORT_KEY_0 + i;
        }
    }
    switch (code) {
    case 0x35: return PORT_KEY_ESCAPE;
    case 0x24: return PORT_KEY_ENTER;
    case 0x31: return PORT_KEY_SPACE;
    case 0x33: return PORT_KEY_BACKSPACE;
    case 0x30: return PORT_KEY_TAB;
    case 0x7E: return PORT_KEY_UP;
    case 0x7D: return PORT_KEY_DOWN;
    case 0x7B: return PORT_KEY_LEFT;
    case 0x7C: return PORT_KEY_RIGHT;
    case 0x74: return PORT_KEY_PAGEUP;
    case 0x79: return PORT_KEY_PAGEDOWN;
    case 0x72: return PORT_KEY_INSERT; // Help key on Apple keyboards
    case 0x75: return PORT_KEY_DELETE;
    case 0x73: return PORT_KEY_HOME;
    case 0x77: return PORT_KEY_END;
    case 0x38: return PORT_KEY_LSHIFT;
    case 0x3C: return PORT_KEY_RSHIFT;
    case 0x3B: return PORT_KEY_LCONTROL;
    case 0x3E: return PORT_KEY_RCONTROL;
    case 0x3A: return PORT_KEY_LALT;
    case 0x3D: return PORT_KEY_RALT;
    case 0x1B: return PORT_KEY_MINUS;
    case 0x18: return PORT_KEY_EQUALS;
    case 0x21: return PORT_KEY_LBRACKET;
    case 0x1E: return PORT_KEY_RBRACKET;
    case 0x2A: return PORT_KEY_BACKSLASH;
    case 0x29: return PORT_KEY_SEMICOLON;
    case 0x27: return PORT_KEY_APOSTROPHE;
    case 0x2B: return PORT_KEY_COMMA;
    case 0x2F: return PORT_KEY_PERIOD;
    case 0x2C: return PORT_KEY_SLASH;
    case 0x32: return PORT_KEY_GRAVE;
    case 0x7A: return PORT_KEY_F1;
    case 0x78: return PORT_KEY_F2;
    case 0x63: return PORT_KEY_F3;
    case 0x76: return PORT_KEY_F4;
    case 0x60: return PORT_KEY_F5;
    case 0x61: return PORT_KEY_F6;
    case 0x62: return PORT_KEY_F7;
    case 0x64: return PORT_KEY_F8;
    case 0x65: return PORT_KEY_F9;
    case 0x6D: return PORT_KEY_F10;
    case 0x67: return PORT_KEY_F11;
    case 0x6F: return PORT_KEY_F12;
    case 0x52: return PORT_KEY_NUMPAD0;
    case 0x53: return PORT_KEY_NUMPAD1;
    case 0x54: return PORT_KEY_NUMPAD2;
    case 0x55: return PORT_KEY_NUMPAD3;
    case 0x56: return PORT_KEY_NUMPAD4;
    case 0x57: return PORT_KEY_NUMPAD5;
    case 0x58: return PORT_KEY_NUMPAD6;
    case 0x59: return PORT_KEY_NUMPAD7;
    case 0x5B: return PORT_KEY_NUMPAD8;
    case 0x5C: return PORT_KEY_NUMPAD9;
    case 0x4C: return PORT_KEY_NUMPADENTER;
    case 0x45: return PORT_KEY_ADD;
    case 0x4E: return PORT_KEY_SUBTRACT;
    case 0x43: return PORT_KEY_MULTIPLY;
    case 0x4B: return PORT_KEY_DIVIDE;
    case 0x41: return PORT_KEY_DECIMAL;
    }
    return -1;
}

// Typed characters, like the WM_CHAR messages TranslateMessage makes on PC.
static std::vector<int> gChars;

static void QueueChars(NSEvent *event) {
    NSString *chars = event.characters;
    std::lock_guard<std::mutex> guard(gInputLock);
    for (NSUInteger i = 0; i < chars.length; i++) {
        unichar c = [chars characterAtIndex:i];
        if (c == 0x7F) {
            c = '\b'; // the Mac delete key is the PC backspace
        } else if (c == 0x03) {
            c = '\r'; // keypad enter
        }
        // Function, arrow and Mac forward-delete keys make no WM_CHAR; nor do
        // characters outside the ANSI code page.
        if (c < 0x100 && !(c >= 0x80 && c < 0xA0)) {
            gChars.push_back(c);
        }
    }
}

static void SetKey(unsigned short code, bool down) {
    int key = PortKeyForKeyCode(code);
    if (key >= 0) {
        std::lock_guard<std::mutex> guard(gInputLock);
        gKeys[key] = down ? 1.0f : 0.0f;
    }
}

// The PC hides the Windows cursor (ShowCursor(FALSE)) and the frontend draws
// its own (FEngDrawMouseCursor); hide the system cursor while it's over the view.
static bool gSystemCursorHidden;

static void SetSystemCursorHidden(bool hidden) {
    if (hidden != gSystemCursorHidden) {
        gSystemCursorHidden = hidden;
        if (hidden) {
            [NSCursor hide];
        } else {
            [NSCursor unhide];
        }
    }
}

@interface PortCursorTracker : NSObject
@end

@implementation PortCursorTracker
- (void)mouseEntered:(NSEvent *)event {
    SetSystemCursorHidden(true);
}
- (void)mouseExited:(NSEvent *)event {
    SetSystemCursorHidden(false);
}
@end

static void TrackMouse(NSEvent *event) {
    NSView *view = event.window.contentView;
    if (view == nil) {
        return;
    }
    NSPoint p = [view convertPoint:event.locationInWindow fromView:nil];
    NSRect b = view.bounds;
    std::lock_guard<std::mutex> guard(gInputLock);
    gMouseU = (float)((p.x - b.origin.x) / b.size.width);
    gMouseV = (float)(view.isFlipped ? (p.y - b.origin.y) / b.size.height : 1.0 - (p.y - b.origin.y) / b.size.height);
    switch (event.type) {
        case NSEventTypeLeftMouseDown: gMouseButtons |= 1; break;
        case NSEventTypeLeftMouseUp: gMouseButtons &= ~1u; break;
        case NSEventTypeRightMouseDown: gMouseButtons |= 2; break;
        case NSEventTypeRightMouseUp: gMouseButtons &= ~2u; break;
        case NSEventTypeOtherMouseDown: if (event.buttonNumber == 2) gMouseButtons |= 4; break;
        case NSEventTypeOtherMouseUp: if (event.buttonNumber == 2) gMouseButtons &= ~4u; break;
        default: break;
    }
}

// Wheel notches for the DirectInput-style lZ: a classic wheel's line deltas
// count as notches; trackpad (precise) deltas are pixels, a notch per 16.
static int gMouseWheel;
static double gMouseWheelPixels;

static void TrackScrollWheel(NSEvent *event) {
    if (event.momentumPhase != NSEventPhaseNone) {
        return; // no inertial scrolling
    }
    double dy = event.scrollingDeltaY;
    if (event.directionInvertedFromDevice) {
        dy = -dy; // natural scrolling: back to the physical direction
    }
    std::lock_guard<std::mutex> guard(gInputLock);
    if (event.hasPreciseScrollingDeltas) {
        gMouseWheelPixels += dy;
        int notches = (int)(gMouseWheelPixels / 16.0);
        gMouseWheelPixels -= notches * 16.0;
        gMouseWheel += notches * 120;
    } else if (dy != 0.0) {
        int notches = (int)std::lround(std::fabs(dy));
        notches = notches < 1 ? 1 : notches;
        gMouseWheel += (dy > 0.0 ? notches : -notches) * 120;
    }
}

// Installed from main.mm once the app is running.
void PortInputInstallKeyboardMonitor() {
    static PortCursorTracker *tracker = [PortCursorTracker new];
    for (NSWindow *window in NSApp.windows) {
        window.acceptsMouseMovedEvents = YES;
        NSView *view = window.contentView;
        if (view != nil) {
            [view addTrackingArea:[[NSTrackingArea alloc]
                                      initWithRect:NSZeroRect
                                           options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect
                                             owner:tracker
                                          userInfo:nil]];
            if (NSPointInRect([window convertPointFromScreen:NSEvent.mouseLocation], view.frame)) {
                SetSystemCursorHidden(true); // already inside: no mouseEntered
            }
        }
    }
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskMouseMoved | NSEventMaskLeftMouseDragged |
                                                  NSEventMaskRightMouseDragged | NSEventMaskOtherMouseDragged |
                                                  NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp |
                                                  NSEventMaskRightMouseDown | NSEventMaskRightMouseUp |
                                                  NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp
                                          handler:^NSEvent *(NSEvent *event) {
                                            TrackMouse(event);
                                            return event; // keep window dragging etc. working
                                          }];
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel
                                          handler:^NSEvent *(NSEvent *event) {
                                            TrackScrollWheel(event);
                                            return event;
                                          }];
    [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown | NSEventMaskKeyUp | NSEventMaskFlagsChanged
                                          handler:^NSEvent *(NSEvent *event) {
                                            if (event.type == NSEventTypeFlagsChanged) {
                                                // Modifier keys: down if their flag is now set.
                                                NSEventModifierFlags f = event.modifierFlags;
                                                bool down = false;
                                                switch (event.keyCode) {
                                                case 0x38: case 0x3C: down = f & NSEventModifierFlagShift; break;
                                                case 0x3B: case 0x3E: down = f & NSEventModifierFlagControl; break;
                                                case 0x3A: case 0x3D: down = f & NSEventModifierFlagOption; break;
                                                }
                                                SetKey(event.keyCode, down);
                                                return event;
                                            }
                                            if (event.modifierFlags & NSEventModifierFlagCommand) {
                                                return event; // keep Cmd-Q etc. working
                                            }
                                            SetKey(event.keyCode, event.type == NSEventTypeKeyDown);
                                            if (event.type == NSEventTypeKeyDown) {
                                                QueueChars(event);
                                            }
                                            return nil; // swallow, so no system beep
                                          }];
}

// Scripted key presses for automated runs, each held for 0.2 s (or /<hold> s):
//   NFSMW_AUTO_KEYS=<seconds>:<key>[/<hold>][,...]  keys: enter esc space up down left right 0..9
//   NFSMW_AUTO_ENTER=<seconds>[,...]       shorthand for enter presses
struct AutoKey {
    double time;
    int key;
    double hold;
};

static int AutoKeyCode(const char *name) {
    static const struct { const char *name; int key; } kNames[] = {
        {"enter", PORT_KEY_ENTER}, {"esc", PORT_KEY_ESCAPE}, {"space", PORT_KEY_SPACE}, {"up", PORT_KEY_UP},
        {"down", PORT_KEY_DOWN},   {"left", PORT_KEY_LEFT},  {"right", PORT_KEY_RIGHT},
    };
    for (const auto &n : kNames) {
        if (strncmp(name, n.name, strlen(n.name)) == 0) {
            return n.key;
        }
    }
    if (name[0] >= '0' && name[0] <= '9') {
        return PORT_KEY_0 + (name[0] - '0');
    }
    return -1;
}

static const std::vector<AutoKey> &AutoKeys() {
    static std::vector<AutoKey> keys = [] {
        std::vector<AutoKey> k;
        auto parse = [&k](const char *env, bool times_only) {
            for (const char *p = env; p && *p;) {
                double t = atof(p);
                int key = PORT_KEY_ENTER;
                double hold = 0.2;
                const char *comma = strchr(p, ',');
                if (!times_only) {
                    const char *colon = strchr(p, ':');
                    key = colon ? AutoKeyCode(colon + 1) : -1;
                    const char *slash = strchr(p, '/');
                    if (slash && (!comma || slash < comma)) {
                        hold = atof(slash + 1);
                    }
                }
                if (key >= 0) {
                    k.push_back({t, key, hold});
                }
                p = comma ? comma + 1 : nullptr;
            }
        };
        parse(getenv("NFSMW_AUTO_ENTER"), true);
        parse(getenv("NFSMW_AUTO_KEYS"), false);
        return k;
    }();
    return keys;
}

// Seconds since the scripted input clock started (the first input poll).
static double AutoInputTime() {
    static const double start = CFAbsoluteTimeGetCurrent();
    return CFAbsoluteTimeGetCurrent() - start;
}

static void ApplyAutoKeys(float *keys) {
    const std::vector<AutoKey> &auto_keys = AutoKeys();
    if (auto_keys.empty()) {
        return;
    }
    double now = AutoInputTime();
    for (const AutoKey &k : auto_keys) {
        if (now >= k.time && now < k.time + k.hold) {
            keys[k.key] = 1.0f;
        }
    }
}

// Scripted mouse for automated runs, on the same clock as NFSMW_AUTO_KEYS:
//   NFSMW_AUTO_MOUSE=<seconds>:<action>[;...]  actions, in 640x480 FE-screen
//   coordinates: move:<x>,<y>  down  up  click (left button held 0.2 s)
//   rclick  wheel:<notches> (+ = away from the user, i.e. scroll up)
// Once a scripted move has happened it overrides the real cursor position.
struct AutoMouse {
    double time;
    enum { MOVE, DOWN, UP, CLICK, RCLICK, WHEEL } action;
    float x, y;
    int notches;
};

static std::vector<AutoMouse> &AutoMice() {
    static std::vector<AutoMouse> events = [] {
        std::vector<AutoMouse> e;
        for (const char *p = getenv("NFSMW_AUTO_MOUSE"); p && *p;) {
            const char *semi = strchr(p, ';');
            const char *colon = strchr(p, ':');
            if (colon && (!semi || colon < semi)) {
                AutoMouse m = {atof(p), AutoMouse::MOVE, 0, 0, 0};
                const char *a = colon + 1;
                bool ok = true;
                if (strncmp(a, "move:", 5) == 0) {
                    m.x = (float)atof(a + 5);
                    const char *comma = strchr(a, ',');
                    m.y = comma ? (float)atof(comma + 1) : 0.0f;
                } else if (strncmp(a, "down", 4) == 0) {
                    m.action = AutoMouse::DOWN;
                } else if (strncmp(a, "up", 2) == 0) {
                    m.action = AutoMouse::UP;
                } else if (strncmp(a, "click", 5) == 0) {
                    m.action = AutoMouse::CLICK;
                } else if (strncmp(a, "rclick", 6) == 0) {
                    m.action = AutoMouse::RCLICK;
                } else if (strncmp(a, "wheel:", 6) == 0) {
                    m.action = AutoMouse::WHEEL;
                    m.notches = atoi(a + 6);
                } else {
                    ok = false;
                }
                if (ok) {
                    e.push_back(m);
                }
            }
            p = semi ? semi + 1 : nullptr;
        }
        return e;
    }();
    return events;
}

// Applies the script to the current state; with take_wheel, returns (and
// consumes) the wheel notches due.
static int ApplyAutoMouse(PortInputState *out, bool take_wheel) {
    std::vector<AutoMouse> &events = AutoMice();
    if (events.empty()) {
        return 0;
    }
    double now = AutoInputTime();
    bool held = false;
    int wheel = 0;
    for (AutoMouse &m : events) {
        if (now < m.time) {
            continue;
        }
        switch (m.action) {
        case AutoMouse::MOVE:
            out->mouse_x = m.x;
            out->mouse_y = m.y;
            break;
        case AutoMouse::DOWN: held = true; break;
        case AutoMouse::UP: held = false; break;
        case AutoMouse::CLICK:
            if (now < m.time + 0.2) out->mouse_buttons |= 1;
            break;
        case AutoMouse::RCLICK:
            if (now < m.time + 0.2) out->mouse_buttons |= 2;
            break;
        case AutoMouse::WHEEL:
            if (take_wheel) {
                wheel += m.notches;
                m.notches = 0;
            }
            break;
        }
    }
    if (held) {
        out->mouse_buttons |= 1;
    }
    return wheel;
}

// Scripted typing for automated runs, on the same clock as NFSMW_AUTO_KEYS:
//   NFSMW_AUTO_TEXT=<seconds>:<text>[;...]  each text is typed at once.
//   Escapes: \r or \n return, \b backspace, \e escape, \; and \\ themselves.
struct AutoText {
    double time;
    std::vector<int> chars;
    bool typed;
};

static std::vector<AutoText> &AutoTexts() {
    static std::vector<AutoText> texts = [] {
        std::vector<AutoText> t;
        const char *p = getenv("NFSMW_AUTO_TEXT");
        while (p && *p) {
            AutoText text = {atof(p), {}, false};
            const char *colon = strchr(p, ':');
            if (colon == nullptr) {
                break;
            }
            for (p = colon + 1; *p && *p != ';'; p++) {
                int c = (unsigned char)*p;
                if (c == '\\' && p[1]) {
                    p++;
                    switch (*p) {
                    case 'r': case 'n': c = '\r'; break;
                    case 'b': c = '\b'; break;
                    case 'e': c = 0x1B; break;
                    default: c = (unsigned char)*p; break;
                    }
                }
                text.chars.push_back(c);
            }
            t.push_back(text);
            if (*p == ';') {
                p++;
            }
        }
        return t;
    }();
    return texts;
}

int PortInputTakeChars(int *out, int max) {
    std::lock_guard<std::mutex> guard(gInputLock);
    std::vector<AutoText> &texts = AutoTexts();
    if (!texts.empty()) {
        double now = AutoInputTime();
        for (AutoText &t : texts) {
            if (!t.typed && now >= t.time) {
                t.typed = true;
                gChars.insert(gChars.end(), t.chars.begin(), t.chars.end());
            }
        }
    }
    int n = (int)gChars.size() < max ? (int)gChars.size() : max;
    std::copy(gChars.begin(), gChars.begin() + n, out);
    gChars.erase(gChars.begin(), gChars.begin() + n);
    return n;
}

int PortInputTakeMouseWheel() {
    std::lock_guard<std::mutex> guard(gInputLock);
    PortInputState scripted = {};
    int wheel = gMouseWheel + ApplyAutoMouse(&scripted, true) * 120;
    gMouseWheel = 0;
    return wheel;
}

void PortInputGetState(PortInputState *out) {
    std::memset(out, 0, sizeof(*out));
    {
        std::lock_guard<std::mutex> guard(gInputLock);
        std::memcpy(out->keys, gKeys, sizeof(gKeys));
        ApplyAutoKeys(out->keys);
        PortMetalViewToFEScreen(gMouseU, gMouseV, &out->mouse_x, &out->mouse_y);
        out->mouse_buttons = gMouseButtons;
        ApplyAutoMouse(out, false);
    }
    GCController *controller = GCController.current;
    if (controller == nil && GCController.controllers.count > 0) {
        controller = GCController.controllers.firstObject;
    }
    GCExtendedGamepad *pad = controller.extendedGamepad;
    if (pad == nil) {
        return;
    }
    out->pad_connected = true;
    float *p = out->pad;
    p[PORT_PAD_A] = pad.buttonA.value;
    p[PORT_PAD_B] = pad.buttonB.value;
    p[PORT_PAD_X] = pad.buttonX.value;
    p[PORT_PAD_Y] = pad.buttonY.value;
    p[PORT_PAD_LSHOULDER] = pad.leftShoulder.value;
    p[PORT_PAD_RSHOULDER] = pad.rightShoulder.value;
    p[PORT_PAD_LTRIGGER] = pad.leftTrigger.value;
    p[PORT_PAD_RTRIGGER] = pad.rightTrigger.value;
    p[PORT_PAD_START] = pad.buttonMenu.value;
    p[PORT_PAD_BACK] = pad.buttonOptions ? pad.buttonOptions.value : 0.0f;
    p[PORT_PAD_LSTICK] = pad.leftThumbstickButton ? pad.leftThumbstickButton.value : 0.0f;
    p[PORT_PAD_RSTICK] = pad.rightThumbstickButton ? pad.rightThumbstickButton.value : 0.0f;
    p[PORT_PAD_DUP] = pad.dpad.up.value;
    p[PORT_PAD_DDOWN] = pad.dpad.down.value;
    p[PORT_PAD_DLEFT] = pad.dpad.left.value;
    p[PORT_PAD_DRIGHT] = pad.dpad.right.value;
    p[PORT_PAD_LX] = pad.leftThumbstick.xAxis.value;
    p[PORT_PAD_LY] = pad.leftThumbstick.yAxis.value;
    p[PORT_PAD_RX] = pad.rightThumbstick.xAxis.value;
    p[PORT_PAD_RY] = pad.rightThumbstick.yAxis.value;
}
