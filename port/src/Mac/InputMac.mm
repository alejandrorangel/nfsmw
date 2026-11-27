// Keyboard (Cocoa key events) and gamepad (GameController framework) state.

#import <Cocoa/Cocoa.h>
#import <GameController/GameController.h>

#include "InputBridge.h"

#include <cstring>
#include <mutex>

static std::mutex gInputLock;
static float gKeys[PORT_KEY_COUNT];

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

static void SetKey(unsigned short code, bool down) {
    int key = PortKeyForKeyCode(code);
    if (key >= 0) {
        std::lock_guard<std::mutex> guard(gInputLock);
        gKeys[key] = down ? 1.0f : 0.0f;
    }
}

// Installed from main.mm once the app is running.
void PortInputInstallKeyboardMonitor() {
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
                                            return nil; // swallow, so no system beep
                                          }];
}

void PortInputGetState(PortInputState *out) {
    std::memset(out, 0, sizeof(*out));
    {
        std::lock_guard<std::mutex> guard(gInputLock);
        std::memcpy(out->keys, gKeys, sizeof(gKeys));
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
