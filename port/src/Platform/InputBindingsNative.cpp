// speed.exe 1.3's PC input device: the action table (GameAction @ 0x91F418,
// initial data 0x8F6D80) whose bindings give every device scalar its value,
// the binding capture the controller screen uses, the joystick defaults and
// the binding names. See src/Speed/Indep/Src/Input/PCInputBindings.h.
//
// The PC reads the keyboard and the first joystick through DirectInput;
// natively the keyboard is PortInputState's keys mapped to DirectInput scan
// codes, and the GameController gamepad is presented as the DirectInput view
// of an Xbox 360 pad (buttons A B X Y LB RB Back Start LS RS, POV d-pad, X/Y
// left stick, Z triggers, Rx/Ry right stick).
//
// The PC saves the bindings in the user profile, as the port does; without
// profiles (NFSMW_MEMCARD=0) the port writes them to $NFSMW_CONTROLS_CONFIG or
// ~/Library/Application Support/NFSMW/Controls.cfg.

#include "InputBindingsNative.h"

#include "Speed/Indep/Src/Frontend/Localization/Localize.hpp"
#include "Speed/Indep/bWare/Inc/Strings.hpp"
#include "Speed/Indep/bWare/Inc/bPrintf.hpp"

#include "../Mac/InputBridge.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>

const char *PortInputGetPadName(); // InputPadMac.mm

namespace {

// size: 0x34
struct PCGameAction {
    const char *Name;
    PCInputBinding Bindings[4];
};
static_assert(sizeof(PCInputBinding) * 4 == 0x30, "a profile saves 0x30 bytes of bindings per action");

#define KB(k) {{PCINPUT_KEY, k, 0}, {}, {}, {}}
#define KB2(k, k2) {{PCINPUT_KEY, k, 0}, {PCINPUT_KEY, k2, 0}, {}, {}}
#define KBJ(k, t, i) {{PCINPUT_KEY, k, 0}, {}, {t, 0, i}, {}}

// 0x8F6D80: copied to GameAction by sub_628410 when the first device is made.
const PCGameAction kDefaultGameActions[PCINPUT_NUM_ACTIONS] = {
    {"GAME_ACTION_GAS", KB(0xC8)},
    {"GAME_ACTION_BRAKE", KB(0xD0)},
    {"GAME_ACTION_STEERLEFT", KB(0xCB)},
    {"GAME_ACTION_STEERRIGHT", KB(0xCD)},
    {"GAME_ACTION_HANDBRAKE", KB(0x39)},
    {"GAME_ACTION_GAMEBREAKER", KB2(0x9D, 0x22)},
    {"GAME_ACTION_NOS", KB2(0x38, 0x2D)},
    {"GAME_ACTION_SHIFTDOWN", KB(0x1D)},
    {"GAME_ACTION_SHIFTUP", KB(0x2A)},
    {"GAME_ACTION_RESET", KB(0x13)},
    {"HUDACTION_ENGAGE_EVENT", KB(0x1C)},
    {"HUDACTION_PAD_DOWN", KB(0x30)},
    {"HUDACTION_PAD_LEFT", KB(0x32)},
    {"HUDACTION_PAD_RIGHT", KB(0x0F)},
    {"FE_ACTION_B2", KB(0x14)},
    {"CAM_ACTION_CHANGE", KB(0x2E)},
    {"CAM_ACTION_LOOKBACK", KB(0x26)},
    {"CAM_ACTION_PULLBACK", KB(0x19)},
    {"HUDACTION_PAUSEREQUEST", KB(0x01)},
    {"FE_ACTION_LEFT", KBJ(0xCB, PCINPUT_POV, 3)},
    {"FE_ACTION_RIGHT", KBJ(0xCD, PCINPUT_POV, 1)},
    {"FE_ACTION_UP", KBJ(0xC8, PCINPUT_POV, 0)},
    {"FE_ACTION_DOWN", KBJ(0xD0, PCINPUT_POV, 2)},
    {"HUDACTION_SKIPNIS", KB(0x1C)},
    {"HUDACTION_NEXTSONG", KB(0x14)},
    {"SC_W", KB(0x11)},
    {"SC_A", KB(0x1E)},
    {"SC_S", KB(0x1F)},
    {"SC_D", KB(0x20)},
    {"FE_ACTION_RTRIGGER", KB(0x3B)},
    {"FE_ACTION_LTRIGGER", KB(0x3C)},
    {"SC_BACKSLASH", KB(0x2B)},
    {"FE_ACTION_ACCEPT", KBJ(0x1C, PCINPUT_BUTTON, 1)},
    {"FE_ACTION_CANCEL", KBJ(0x01, PCINPUT_BUTTON, 2)},
    {"FE_ACTION_START", KBJ(0x39, PCINPUT_BUTTON, 9)},
    {"FE_ACTION_LTRIG", KBJ(0x0A, PCINPUT_BUTTON, 4)},
    {"FE_ACTION_RTRIG", KBJ(0x0B, PCINPUT_BUTTON, 5)},
    {"FE_ACTION_B0", KB(0x04)},
    {"FE_ACTION_B1", KB(0x32)},
    {"FE_ACTION_B3", KB(0x05)},
    {"FE_ACTION_B4", KB(0x03)},
    {"FE_ACTION_B5", KBJ(0x02, PCINPUT_BUTTON, 0)},
    {"DEBUG_ACTION_TOGGLECAM", KB(0x18)},
    {"DEBUG_ACTION_TOGGLECARCOLOR", KB(0x17)},
    {"SC_Z", KB(0x2C)},
    {"SC_X", KB(0x2D)},
    {"SC_C", KB(0x2E)},
    {"SC_V", KB(0x2F)},
    {"SC_F1", KB(0x3B)},
    {"SC_MINUS", KB(0x0C)},
    {"SC_EQUALS", KB(0x0D)},
    {"SC_LBRACKET", KB(0x1A)},
    {"SC_RBRACKET", KB(0x1B)},
    {"SC_LALT", KB(0x38)},
    {"SC_RALT", KB(0xB8)},
    {"SC_NUMPADENTER", KB(0x9C)},
    {"SC_PAGEUP", KB(0xC9)},
    {"SC_PAGEDOWN", KB(0xD1)},
    {"SC_RETURN", KB(0x1C)},
    {"SC_ESCAPE", KB(0x01)},
    {"SC_SPACE", KB(0x39)},
    {"SC_INSERT", KB(0xD2)},
    {"SC_BACK", KB(0x0E)},
    {"SC_NUMPAD1", KBJ(0x4F, PCINPUT_BUTTON, 6)},
    {"SC_NUMPAD2", KBJ(0x50, PCINPUT_AXIS_HIGH, 1)},
    {"SC_NUMPAD3", KBJ(0x51, PCINPUT_ROT_HIGH, 2)},
    {"SC_NUMPAD4", KBJ(0x4B, PCINPUT_AXIS_LOW, 0)},
    {"SC_NUMPAD5", KB(0x4C)},
    {"SC_NUMPAD6", KBJ(0x4D, PCINPUT_AXIS_HIGH, 0)},
    {"SC_NUMPAD7", KBJ(0x47, PCINPUT_BUTTON, 4)},
    {"SC_NUMPAD8", KBJ(0x48, PCINPUT_AXIS_LOW, 1)},
    {"SC_NUMPAD9", KBJ(0x49, PCINPUT_ROT_LOW, 2)},
    {"SC_NUMPAD0", KBJ(0x52, PCINPUT_AXIS_LOW, 2)},
    {"SC_DECIMAL", KBJ(0x53, PCINPUT_AXIS_HIGH, 2)},
    {"SC_SUBTRACT", KB(0x4A)},
    {"SC_ADD", KB(0x4E)},
};

#undef KB
#undef KB2
#undef KBJ

PCGameAction gGameActions[PCINPUT_NUM_ACTIONS]; // GameAction 0x91F418

// The PC keyboard device's capture state (device + 0x183C..0x1848).
bool gCapturing;
int gCaptureDevice;
int gCaptureSlot;
int gCaptureAction;

// What the joystick bindings were set up for (the PC profile saves
// sub_634DA0(3) with them).
uint32 gJoystickIdentity;

// The joystick as DirectInput reports it (the PC device's state layout:
// X Y Z at 0, buttons at 0xC, Rx Ry Rz at 0x20C, sliders 0x218, POVs 0x220).
struct JoyState {
    int Axis[3];
    int Buttons[128];
    int Rot[3];
    int Sliders[2];
    unsigned int POV[4];
};

struct RawState {
    unsigned char Keys[256]; // by DirectInput scan code
    bool PadConnected;
    JoyState Joy;
};

RawState gRaw;
RawState gPrevRaw;
JoyState gJoyRest; // dword_91F1E8: the joystick's state when the device was initialised

// PortKey -> DirectInput scan code (DIK_*).
const unsigned char kScanCodes[PORT_KEY_COUNT] = {
    0x01, 0x1C, 0x39, 0x0E, 0x0F,                   // ESCAPE ENTER SPACE BACKSPACE TAB
    0xC8, 0xD0, 0xCB, 0xCD,                         // UP DOWN LEFT RIGHT
    0xC9, 0xD1, 0xD2, 0xD3, 0xC7, 0xCF,             // PAGEUP PAGEDOWN INSERT DELETE HOME END
    0x2A, 0x36, 0x1D, 0x9D, 0x38, 0xB8,             // LSHIFT RSHIFT LCONTROL RCONTROL LALT RALT
    0x0C, 0x0D, 0x1A, 0x1B, 0x2B,                   // MINUS EQUALS LBRACKET RBRACKET BACKSLASH
    0x27, 0x28, 0x33, 0x34, 0x35, 0x29,             // SEMICOLON APOSTROPHE COMMA PERIOD SLASH GRAVE
    0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40,             // F1..F6
    0x41, 0x42, 0x43, 0x44, 0x57, 0x58,             // F7..F12
    0x52, 0x4F, 0x50, 0x51, 0x4B,                   // NUMPAD0..4
    0x4C, 0x4D, 0x47, 0x48, 0x49,                   // NUMPAD5..9
    0x9C, 0x4E, 0x4A, 0x37, 0xB5, 0x53,             // NUMPADENTER ADD SUBTRACT MULTIPLY DIVIDE DECIMAL
    0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, // A..H
    0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, // I..P
    0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, // Q..X
    0x15, 0x2C,                                     // Y Z
    0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, // 0..9
};

// The keyboard's DirectInput object names (GetObjectInfo, from the US
// keyboard layout's GetKeyNameText). TODO(port): from memory of Windows'
// English key names, not captured from a PC.
const char *KeyName(int code) {
    static const char *const kNames[256] = {
        nullptr, "Esc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "Backspace", "Tab",
        "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "Enter", "Ctrl", "A", "S",
        "D", "F", "G", "H", "J", "K", "L", ";", "'", "`", "Shift", "\\", "Z", "X", "C", "V",
        "B", "N", "M", ",", ".", "/", "Right Shift", "Num *", "Alt", "Space", "Caps Lock", "F1", "F2", "F3", "F4", "F5",
        "F6", "F7", "F8", "F9", "F10", "Num Lock", "Scroll Lock", "Num 7", "Num 8", "Num 9", "Num -", "Num 4", "Num 5", "Num 6", "Num +", "Num 1",
        "Num 2", "Num 3", "Num 0", "Num Del", nullptr, nullptr, nullptr, "F11", "F12",
    };
    switch (code) {
    case 0x9C: return "Num Enter";
    case 0x9D: return "Right Ctrl";
    case 0xB5: return "Num /";
    case 0xB8: return "Right Alt";
    case 0xC7: return "Home";
    case 0xC8: return "Up";
    case 0xC9: return "Page Up";
    case 0xCB: return "Left";
    case 0xCD: return "Right";
    case 0xCF: return "End";
    case 0xD0: return "Down";
    case 0xD1: return "Page Down";
    case 0xD2: return "Insert";
    case 0xD3: return "Delete";
    default: break;
    }
    return code >= 0 && code < 256 ? kNames[code] : nullptr;
}

// sub_627CE0: language strings for the keys sub_627FF0 can't spell.
uint32 KeyLanguageHash(const PCInputBinding &b) {
    if (b.Type != PCINPUT_KEY) {
        return 0;
    }
    switch (b.Code) {
    case 0x0E: return 0x8B9759F5;
    case 0x0F: return 0x8FDE18FB;
    case 0x1C: return 0xF47A1F24;
    case 0x1D: return 0xCBD7BEF1;
    case 0x2A: return 0xE771B94E;
    case 0x36: return 0xF570AD14;
    case 0x38: return 0x8B9CD6F1;
    case 0x39: return 0xFFBE6990;
    case 0x3A: return 0xF770E262;
    case 0x3B: return 0x59B163BB;
    case 0x3C: return 0x59B163BC;
    case 0x3D: return 0x59B163BD;
    case 0x3E: return 0x59B163BE;
    case 0x3F: return 0x59B163BF;
    case 0x40: return 0x59B163C0;
    case 0x41: return 0x59B163C1;
    case 0x42: return 0x59B163C2;
    case 0x43: return 0x59B163C3;
    case 0x44: return 0x8FDDDB4B;
    case 0x45: return 0x74A3F1DD;
    case 0x46: return 0xF6A9E3B3;
    case 0x47: return 0x74A5E7E0;
    case 0x48: return 0x74A5E7E1;
    case 0x49: return 0x74A5E7E2;
    case 0x4B: return 0x74A5E7DD;
    case 0x4C: return 0x74A5E7DE;
    case 0x4D: return 0x74A5E7DF;
    case 0x4F: return 0x74A5E7DA;
    case 0x50: return 0x74A5E7DB;
    case 0x51: return 0x74A5E7DC;
    case 0x52: return 0x74A5E7D9;
    case 0x53: return 0x4CD46553;
    case 0x57: return 0x8FDDDB4C;
    case 0x58: return 0x8FDDDB4D;
    case 0x64: return 0x8FDDDB4E;
    case 0x65: return 0x8FDDDB4F;
    case 0x66: return 0x8FDDDB50;
    case 0x9C: return 0xA4406307;
    case 0x9D: return 0x5562BC37;
    case 0xB5: return 0xD41B2C39;
    case 0xB8: return 0x8BA02137;
    case 0xC5: return 0xFF803D82;
    case 0xC7: return 0x8B9AE10D;
    case 0xC8: return 0x59B165C9;
    case 0xC9: return 0xFF895C70;
    case 0xCB: return 0x8B9CE72F;
    case 0xCD: return 0xFFA894A2;
    case 0xCF: return 0x8FDDDADB;
    case 0xD0: return 0x8B98B0DC;
    case 0xD1: return 0x8B9E0243;
    case 0xD2: return 0xE01DBD99;
    case 0xD3: return 0xD3CD3F77;
    default: return 0;
    }
}

// The character ToUnicode gives a key with no modifiers held (US layout).
char KeyCharacter(int code) {
    const char *name = KeyName(code);
    if (name != nullptr && name[0] != '\0' && name[1] == '\0') {
        char c = name[0];
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }
    switch (code) {
    case 0x0E: return '\b';
    case 0x0F: return '\t';
    case 0x1C: return '\r';
    case 0x39: return ' ';
    case 0x37: return '*';
    case 0x4A: return '-';
    case 0x4E: return '+';
    case 0xB5: return '/';
    default: return '\0';
    }
}

int Slot(int device, bool primary) {
    if (device == PCINPUT_KEYBOARD) {
        return primary ? 0 : 1;
    }
    return primary ? 2 : 3;
}

bool SameBinding(const PCInputBinding &a, const PCInputBinding &b) {
    return a.Type == b.Type && a.Code == b.Code && a.Index == b.Index;
}

void ClearSlot(PCInputBinding &b) {
    b.Type = 0;
    b.Code = 0;
    b.Index = 0;
}

int ClampAxis(float v) {
    long i = std::lround(v);
    return i < 0 ? 0 : (i > 0xFFFF ? 0xFFFF : static_cast<int>(i));
}

// The GameController pad as an Xbox 360 pad's DirectInput state.
void ReadJoystick(const PortInputState &in, JoyState &joy) {
    std::memset(&joy, 0, sizeof(joy));
    const float *p = in.pad;
    joy.Axis[0] = ClampAxis((p[PORT_PAD_LX] + 1.0f) * 32767.5f);
    joy.Axis[1] = ClampAxis((1.0f - p[PORT_PAD_LY]) * 32767.5f);
    joy.Axis[2] = ClampAxis((1.0f + p[PORT_PAD_LTRIGGER] - p[PORT_PAD_RTRIGGER]) * 32767.5f);
    joy.Rot[0] = ClampAxis((p[PORT_PAD_RX] + 1.0f) * 32767.5f);
    joy.Rot[1] = ClampAxis((1.0f - p[PORT_PAD_RY]) * 32767.5f);
    joy.Rot[2] = 0x7FFF;
    joy.Sliders[0] = 0x7FFF;
    joy.Sliders[1] = 0x7FFF;
    static const int kButtons[] = {PORT_PAD_A,         PORT_PAD_B,     PORT_PAD_X,     PORT_PAD_Y,      PORT_PAD_LSHOULDER,
                                   PORT_PAD_RSHOULDER, PORT_PAD_BACK, PORT_PAD_START, PORT_PAD_LSTICK, PORT_PAD_RSTICK};
    for (int i = 0; i < static_cast<int>(sizeof(kButtons) / sizeof(kButtons[0])); i++) {
        joy.Buttons[i] = p[kButtons[i]] >= 0.5f ? 0x80 : 0;
    }
    const bool up = p[PORT_PAD_DUP] >= 0.5f;
    const bool down = p[PORT_PAD_DDOWN] >= 0.5f;
    const bool left = p[PORT_PAD_DLEFT] >= 0.5f;
    const bool right = p[PORT_PAD_DRIGHT] >= 0.5f;
    unsigned int pov = 0xFFFFFFFF;
    if (up && right) {
        pov = 4500;
    } else if (down && right) {
        pov = 13500;
    } else if (down && left) {
        pov = 22500;
    } else if (up && left) {
        pov = 31500;
    } else if (up) {
        pov = 0;
    } else if (right) {
        pov = 9000;
    } else if (down) {
        pov = 18000;
    } else if (left) {
        pov = 27000;
    }
    for (int i = 0; i < 4; i++) {
        joy.POV[i] = 0xFFFFFFFF;
    }
    joy.POV[0] = pov;
}

void ReadRaw(RawState &raw) {
    PortInputState in;
    PortInputGetState(&in);
    std::memset(&raw, 0, sizeof(raw));
    for (int k = 0; k < PORT_KEY_COUNT; k++) {
        if (in.keys[k] >= 0.5f) {
            raw.Keys[kScanCodes[k]] = 0x80;
        }
    }
    raw.PadConnected = in.pad_connected;
    ReadJoystick(in, raw.Joy);
    if (!raw.PadConnected) {
        raw.Joy.Axis[0] = raw.Joy.Axis[1] = raw.Joy.Axis[2] = 0x7FFF;
        raw.Joy.Rot[0] = raw.Joy.Rot[1] = 0x7FFF;
    }
}

const char *JoystickName() {
    if (!gRaw.PadConnected) {
        return "";
    }
    const char *name = PortInputGetPadName();
    return name != nullptr ? name : "";
}

// sub_634DA0(3)
uint32 JoystickIdentity() {
    return bStringHashUpper(JoystickName());
}

// sub_628940: one binding's value.
float BindingValue(const PCInputBinding &b) {
    const JoyState &joy = gRaw.Joy;
    const float kAxisCentre = 32767.5f;        // 0x8A7A58
    const float kAxisScale = 1.0f / 32767.5f;  // 0x8957BC
    float sign = 1.0f;
    float v;
    switch (b.Type) {
    case PCINPUT_KEY:
        return b.Code >= 0 && b.Code < 256 && gRaw.Keys[b.Code] != 0 ? 1.0f : 0.0f;
    case PCINPUT_BUTTON:
        if (!gRaw.PadConnected || b.Index < 0 || b.Index >= 128) {
            return 0.0f;
        }
        return static_cast<float>(joy.Buttons[b.Index]) * 0.0078125f; // 0x8A7A5C
    case PCINPUT_POV: {
        if (!gRaw.PadConnected) {
            return 0.0f;
        }
        const unsigned int pov = joy.POV[0];
        switch (b.Index) {
        case 0: return (pov == 31500 || pov == 0 || pov == 4500) ? 1.0f : 0.0f;
        case 1: return (pov == 4500 || pov == 9000 || pov == 13500) ? 1.0f : 0.0f;
        case 2: return (pov == 13500 || pov == 18000 || pov == 22500) ? 1.0f : 0.0f;
        case 3: return (pov == 22500 || pov == 27000 || pov == 31500) ? 1.0f : 0.0f;
        default: return 0.0f;
        }
    }
    case PCINPUT_AXIS_HIGH:
        sign = -1.0f;
    case PCINPUT_AXIS_LOW:
        if (!gRaw.PadConnected || b.Index < 0 || b.Index > 2) {
            return 0.0f;
        }
        v = (kAxisCentre - static_cast<float>(joy.Axis[b.Index])) * sign;
        return v <= 0.0f ? 0.0f : v * kAxisScale;
    case PCINPUT_ROT_HIGH:
        sign = -1.0f;
    case PCINPUT_ROT_LOW:
        if (!gRaw.PadConnected || b.Index < 0 || b.Index > 2) {
            return 0.0f;
        }
        v = (kAxisCentre - static_cast<float>(joy.Rot[b.Index])) * sign;
        return v <= 0.0f ? 0.0f : v * kAxisScale;
    case PCINPUT_SLIDER_HIGH:
        sign = -1.0f;
    case PCINPUT_SLIDER_LOW:
        if (!gRaw.PadConnected || b.Index < 0 || b.Index > 1) {
            return 0.0f;
        }
        v = (kAxisCentre - static_cast<float>(joy.Sliders[b.Index])) * sign;
        return v <= 0.0f ? 0.0f : v * kAxisScale;
    default:
        return 0.0f;
    }
}

// sub_6285F0: a captured input becomes the capturing action's binding; the
// device's other slot of that action and the other rebindable actions lose it.
void AssignBinding(int device_type, const PCInputBinding &b) {
    const int action = gCaptureAction;
    const int primary = device_type == PCINPUT_JOYSTICK ? 2 : 0;
    const int secondary = device_type == PCINPUT_JOYSTICK ? 3 : 1;
    PCInputBinding *slots = gGameActions[action].Bindings;
    bool assigned = false;
    if (gCaptureSlot == secondary) {
        if (slots[primary].Type == PCINPUT_UNBOUND) {
            slots[primary] = b;
            assigned = true;
        } else if (SameBinding(slots[primary], b)) {
            ClearSlot(slots[secondary]);
            assigned = true;
        }
    }
    if (!assigned && gCaptureSlot == primary && SameBinding(slots[secondary], b)) {
        slots[primary] = b;
        ClearSlot(slots[secondary]);
        assigned = true;
    }
    if (!assigned) {
        slots[gCaptureSlot] = b;
    }
    for (int i = 0; i < PCINPUT_NUM_CONFIG_ACTIONS; i++) {
        if (i == action) {
            continue;
        }
        PCInputBinding *other = gGameActions[i].Bindings;
        if (SameBinding(other[primary], b)) {
            other[primary] = other[secondary];
            ClearSlot(other[secondary]);
        } else if (SameBinding(other[secondary], b)) {
            ClearSlot(other[secondary]);
        }
    }
}

bool AxisMoved(int value, int rest) {
    return (value < 0x201 || value > 0xFDFE) && value != rest;
}

// sub_634490: a joystick event during a joystick capture. The PC gets
// DirectInput buffered events; natively the state changes since the last poll
// stand in for them.
bool CaptureJoystick() {
    const JoyState &joy = gRaw.Joy;
    const JoyState &prev = gPrevRaw.Joy;
    PCInputBinding b = {0, 0, 0};
    for (int i = 0; i < 128; i++) {
        if (joy.Buttons[i] != 0 && prev.Buttons[i] == 0) {
            b.Type = PCINPUT_BUTTON;
            b.Index = i;
            AssignBinding(PCINPUT_JOYSTICK, b);
            return true;
        }
    }
    if (joy.Axis[0] != prev.Axis[0] || joy.Axis[1] != prev.Axis[1] || joy.Axis[2] != prev.Axis[2]) {
        for (int i = 0; i < 3; i++) {
            if (AxisMoved(joy.Axis[i], gJoyRest.Axis[i])) {
                b.Type = joy.Axis[i] < 0x201 ? PCINPUT_AXIS_LOW : PCINPUT_AXIS_HIGH;
                b.Index = i;
                AssignBinding(PCINPUT_JOYSTICK, b);
                return true;
            }
        }
    }
    if (joy.Rot[0] != prev.Rot[0] || joy.Rot[1] != prev.Rot[1] || joy.Rot[2] != prev.Rot[2]) {
        for (int i = 0; i < 3; i++) {
            if (AxisMoved(joy.Rot[i], gJoyRest.Rot[i])) {
                b.Type = joy.Rot[i] < 0x201 ? PCINPUT_ROT_LOW : PCINPUT_ROT_HIGH;
                b.Index = i;
                AssignBinding(PCINPUT_JOYSTICK, b);
                return true;
            }
        }
    }
    if (joy.POV[0] != prev.POV[0]) {
        const unsigned int pov = joy.POV[0];
        if (pov == 0 || pov == 9000 || pov == 18000 || pov == 27000) {
            b.Type = PCINPUT_POV;
            b.Index = static_cast<int>(pov / 9000);
            AssignBinding(PCINPUT_JOYSTICK, b);
            return true;
        }
    }
    return false;
}

// sub_634900: the device's poll while capturing. Escape cancels; a key binds
// on the keyboard page, a joystick input on the joystick page.
void PollCapture() {
    for (int code = 0; code < 256; code++) {
        const bool down = gRaw.Keys[code] != 0;
        const bool was_down = gPrevRaw.Keys[code] != 0;
        if (down == was_down) {
            continue;
        }
        if (code == 0x01) {
            gCapturing = false;
            return;
        }
        if (gCaptureDevice == PCINPUT_KEYBOARD && down) {
            PCInputBinding b = {PCINPUT_KEY, code, 0};
            AssignBinding(PCINPUT_KEYBOARD, b);
            gCapturing = false;
            return;
        }
    }
    if (gCaptureDevice == PCINPUT_JOYSTICK && gRaw.PadConnected && CaptureJoystick()) {
        gCapturing = false;
    }
}

// sub_63CD80's table for "XBOX 360 For Windows (Controller)" (bStringHashUpper
// 0x3F5996E0): joystick slots of the first 24 actions.
void SetJoystickDefaults(const PCInputBinding (*defaults)[2], int count) {
    for (int i = 0; i < count && i < PCINPUT_NUM_ACTIONS; i++) {
        gGameActions[i].Bindings[2] = defaults[i][0];
        gGameActions[i].Bindings[3] = defaults[i][1];
    }
}

#define J(t, i) {t, 0, i}
#define NONE {0, 0, 0}
const PCInputBinding kXbox360Defaults[24][2] = {
    {J(PCINPUT_AXIS_LOW, 2), NONE},  // GAS: right trigger
    {J(PCINPUT_AXIS_HIGH, 2), NONE}, // BRAKE: left trigger
    {J(PCINPUT_AXIS_LOW, 0), NONE},  // STEERLEFT
    {J(PCINPUT_AXIS_HIGH, 0), NONE}, // STEERRIGHT
    {J(PCINPUT_BUTTON, 0), NONE},    // HANDBRAKE: A
    {J(PCINPUT_BUTTON, 2), NONE},    // GAMEBREAKER: X
    {J(PCINPUT_BUTTON, 1), NONE},    // NOS: B
    {J(PCINPUT_ROT_HIGH, 1), NONE},  // SHIFTDOWN: right stick down
    {J(PCINPUT_ROT_LOW, 1), NONE},   // SHIFTUP: right stick up
    {J(PCINPUT_BUTTON, 6), NONE},    // RESET: Back
    {J(PCINPUT_POV, 0), NONE},       // ENGAGE_EVENT
    {J(PCINPUT_POV, 2), NONE},       // PAD_DOWN
    {J(PCINPUT_POV, 3), NONE},       // PAD_LEFT
    {J(PCINPUT_POV, 1), NONE},       // PAD_RIGHT
    {J(PCINPUT_BUTTON, 8), NONE},    // FE_ACTION_B2: left stick
    {J(PCINPUT_BUTTON, 4), NONE},    // CAM_ACTION_CHANGE: LB
    {J(PCINPUT_BUTTON, 3), NONE},    // LOOKBACK: Y
    {J(PCINPUT_BUTTON, 5), NONE},    // PULLBACK: RB
    {J(PCINPUT_BUTTON, 7), NONE},    // PAUSEREQUEST: Start
    {J(PCINPUT_POV, 3), J(PCINPUT_AXIS_LOW, 0)},  // FE_ACTION_LEFT
    {J(PCINPUT_POV, 1), J(PCINPUT_AXIS_HIGH, 0)}, // FE_ACTION_RIGHT
    {J(PCINPUT_POV, 0), J(PCINPUT_AXIS_LOW, 1)},  // FE_ACTION_UP
    {J(PCINPUT_POV, 2), J(PCINPUT_AXIS_HIGH, 1)}, // FE_ACTION_DOWN
    {NONE, NONE},                                 // HUDACTION_SKIPNIS
};

// sub_63CD80's fallback for an unknown joystick without a POV hat
// (sub_628530 false); with no joystick attached the PC uses this one too.
const PCInputBinding kNoPOVDefaults[24][2] = {
    {J(PCINPUT_BUTTON, 0), NONE},    {J(PCINPUT_BUTTON, 2), NONE},    {J(PCINPUT_AXIS_LOW, 0), NONE},
    {J(PCINPUT_AXIS_HIGH, 0), NONE}, {J(PCINPUT_BUTTON, 1), NONE},    {J(PCINPUT_BUTTON, 3), NONE},
    {J(PCINPUT_BUTTON, 6), NONE},    {J(PCINPUT_AXIS_HIGH, 1), NONE}, {J(PCINPUT_AXIS_LOW, 1), NONE},
    {J(PCINPUT_BUTTON, 5), NONE},    {NONE, NONE},                    {NONE, NONE},
    {NONE, NONE},                    {NONE, NONE},                    {NONE, NONE},
    {NONE, NONE},                    {NONE, NONE},                    {NONE, NONE},
    {J(PCINPUT_BUTTON, 9), NONE},    {J(PCINPUT_AXIS_LOW, 0), NONE},  {J(PCINPUT_AXIS_HIGH, 0), NONE},
    {J(PCINPUT_AXIS_LOW, 1), NONE},  {J(PCINPUT_AXIS_HIGH, 1), NONE}, {NONE, NONE},
};
#undef J
#undef NONE

void DefaultJoystick() {
    // The PC picks the table by the joystick's product name; the
    // GameController pad has the Xbox layout whatever its name, so it gets
    // the Xbox 360 table. TODO(port): guess.
    if (gRaw.PadConnected) {
        SetJoystickDefaults(kXbox360Defaults, 24);
    } else {
        SetJoystickDefaults(kNoPOVDefaults, 24);
    }
    gJoystickIdentity = JoystickIdentity();
}

std::string ConfigPath() {
    if (const char *p = std::getenv("NFSMW_CONTROLS_CONFIG")) {
        return p;
    }
    const char *home = std::getenv("HOME");
    std::string dir = std::string(home ? home : ".") + "/Library/Application Support/NFSMW";
    mkdir(dir.c_str(), 0755);
    return dir + "/Controls.cfg";
}

// sub_56E200 (UserProfile::LoadFromBuffer): the saved bindings of the
// rebindable actions; a different joystick gets its defaults.
void LoadBindings() {
    FILE *f = std::fopen(ConfigPath().c_str(), "r");
    if (f == nullptr) {
        return;
    }
    uint32 saved_identity = gJoystickIdentity;
    char name[64];
    while (std::fscanf(f, "%63s", name) == 1) {
        if (std::strcmp(name, "JOYSTICK") == 0) {
            unsigned int id;
            if (std::fscanf(f, "%x", &id) == 1) {
                saved_identity = id;
            }
            continue;
        }
        int v[12];
        if (std::fscanf(f, "%d %d %d %d %d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8],
                        &v[9], &v[10], &v[11]) != 12) {
            break;
        }
        for (int i = 0; i < PCINPUT_NUM_CONFIG_ACTIONS; i++) {
            if (std::strcmp(gGameActions[i].Name, name) == 0) {
                for (int s = 0; s < 4; s++) {
                    gGameActions[i].Bindings[s].Type = v[s * 3];
                    gGameActions[i].Bindings[s].Code = v[s * 3 + 1];
                    gGameActions[i].Bindings[s].Index = v[s * 3 + 2];
                }
            }
        }
    }
    std::fclose(f);
    gJoystickIdentity = saved_identity;
    const uint32 current = JoystickIdentity();
    if (saved_identity != current && current != 0) {
        DefaultJoystick();
    }
}

} // namespace

PCInputBinding *PCInputGetBindings(int action) {
    return gGameActions[action].Bindings;
}

uint32 PCInputGetActionNameHash(int action) {
    return bStringHash(gGameActions[action].Name);
}

void PCInputGetBindingName(char *name, int device, int action, bool primary) {
    const PCInputBinding &b = gGameActions[action].Bindings[Slot(device, primary)];
    if (b.Type == PCINPUT_UNBOUND) {
        bStrNCpy(name, "---", PCINPUT_BINDING_NAME_SIZE);
        return;
    }
    name[0] = '\0';
    if (device == PCINPUT_KEYBOARD) {
        const eLanguages language = GetCurrentLanguage();
        if (language == eLANGUAGE_POLISH || language == eLANGUAGE_DUTCH) {
            // sub_627FF0: the key's character, else its language string.
            const char c = KeyCharacter(b.Code);
            if (c != '\0' && c != ' ' && c != '\b' && c != '\t' && c != '\r') {
                name[0] = c;
                name[1] = '\0';
                return;
            }
            const uint32 hash = KeyLanguageHash(b);
            if (hash != 0) {
                GetLocalizedString(name, PCINPUT_BINDING_NAME_SIZE, hash);
            }
            return;
        }
        // sub_6280C0: the keyboard object's DirectInput name.
        const char *key = KeyName(b.Code);
        bStrNCpy(name, key != nullptr ? key : "", PCINPUT_BINDING_NAME_SIZE);
        return;
    }
    // sub_6280C0: the joystick object's DirectInput name; button names count
    // from 1 when the joystick's first button's name ends in 0 (sub_627C40),
    // as the Xbox 360 pad's do. TODO(port): the names are the Xbox 360 pad's
    // as recalled, not captured from a PC.
    switch (b.Type) {
    case PCINPUT_BUTTON:
        bSNPrintf(name, PCINPUT_BINDING_NAME_SIZE, "Button %d", b.Index + 1);
        break;
    case PCINPUT_POV:
        bStrNCpy(name, "Hat Switch", PCINPUT_BINDING_NAME_SIZE);
        break;
    case PCINPUT_AXIS_LOW:
    case PCINPUT_AXIS_HIGH: {
        static const char *const kAxes[] = {"X Axis", "Y Axis", "Z Axis"};
        bStrNCpy(name, b.Index >= 0 && b.Index < 3 ? kAxes[b.Index] : "", PCINPUT_BINDING_NAME_SIZE);
        break;
    }
    case PCINPUT_ROT_LOW:
    case PCINPUT_ROT_HIGH: {
        static const char *const kRotations[] = {"X Rotation", "Y Rotation", "Z Rotation"};
        bStrNCpy(name, b.Index >= 0 && b.Index < 3 ? kRotations[b.Index] : "", PCINPUT_BINDING_NAME_SIZE);
        break;
    }
    case PCINPUT_SLIDER_LOW:
    case PCINPUT_SLIDER_HIGH:
        bStrNCpy(name, "Slider", PCINPUT_BINDING_NAME_SIZE);
        break;
    default:
        break;
    }
}

void PCInputClearBinding(int device, int action, bool primary) {
    const int slot = Slot(device, primary);
    PCInputBinding *slots = gGameActions[action].Bindings;
    if (slot == 0 || slot == 2) {
        slots[slot] = slots[slot + 1];
        ClearSlot(slots[slot + 1]);
    } else {
        ClearSlot(slots[slot]);
    }
}

void PCInputDefaultKeyboardBindings() {
    for (int i = 0; i < PCINPUT_NUM_CONFIG_ACTIONS; i++) {
        gGameActions[i].Bindings[0] = kDefaultGameActions[i].Bindings[0];
        gGameActions[i].Bindings[1] = kDefaultGameActions[i].Bindings[1];
    }
}

void PCInputDefaultJoystickBindings() {
    DefaultJoystick();
}

void PCInputStartCapture(int device, int action, bool primary) {
    gCapturing = true;
    gCaptureDevice = device;
    gCaptureAction = action;
    if (device == PCINPUT_KEYBOARD) {
        gCaptureSlot = primary ? 0 : 1;
    } else if (device == PCINPUT_JOYSTICK) {
        gCaptureSlot = primary ? 2 : 3;
    }
}

bool PCInputIsCapturing() {
    return gCapturing;
}

bool PCInputJoystickPresent() {
    return gRaw.PadConnected;
}

const char *PCInputGetDeviceName(int device) {
    if (device == PCINPUT_KEYBOARD) {
        return "Keyboard"; // TODO(port): the DirectInput keyboard's product name, guessed
    }
    if (device == PCINPUT_JOYSTICK && gRaw.PadConnected) {
        return JoystickName();
    }
    return "";
}

// sub_56E180 (UserProfile::SaveToBuffer): the joystick identity and the
// rebindable actions' bindings.
char *PCInputSaveBindingsToBuffer(char *buf, char *maxbuf) {
    const uint32 identity = JoystickIdentity();
    if (buf + sizeof(identity) <= maxbuf) {
        std::memcpy(buf, &identity, sizeof(identity));
        buf += sizeof(identity);
    }
    for (int i = 0; i < PCINPUT_NUM_CONFIG_ACTIONS; i++) {
        const int size = sizeof(gGameActions[i].Bindings);
        if (buf + size <= maxbuf) {
            std::memcpy(buf, gGameActions[i].Bindings, size);
            buf += size;
        }
    }
    return buf;
}

// sub_56E200 (UserProfile::LoadFromBuffer): a different joystick than the one
// the bindings were saved for gets its defaults (sub_63CD80).
char *PCInputLoadBindingsFromBuffer(char *buf, char *maxbuf) {
    uint32 saved_identity = 0;
    char *p = buf + sizeof(saved_identity);
    if (p <= maxbuf) {
        std::memcpy(&saved_identity, buf, sizeof(saved_identity));
    }
    for (int i = 0; i < PCINPUT_NUM_CONFIG_ACTIONS; i++) {
        const int size = sizeof(gGameActions[i].Bindings);
        if (p + size <= maxbuf) {
            std::memcpy(gGameActions[i].Bindings, p, size);
        }
        p += size;
    }
    gJoystickIdentity = saved_identity;
    const uint32 current = JoystickIdentity();
    if (saved_identity != current && current != 0) {
        DefaultJoystick();
    }
    return p;
}

void PCInputSaveBindings() {
    extern int IsMemcardEnabled;
    if (IsMemcardEnabled) {
        return; // saved with the profile
    }
    const std::string path = ConfigPath();
    FILE *f = std::fopen(path.c_str(), "w");
    if (f == nullptr) {
        std::fprintf(stderr, "[input] can't write %s\n", path.c_str());
        return;
    }
    std::fprintf(f, "JOYSTICK %08x\n", JoystickIdentity());
    for (int i = 0; i < PCINPUT_NUM_CONFIG_ACTIONS; i++) {
        std::fprintf(f, "%s", gGameActions[i].Name);
        for (int s = 0; s < 4; s++) {
            const PCInputBinding &b = gGameActions[i].Bindings[s];
            std::fprintf(f, " %d %d %d", b.Type, b.Code, b.Index);
        }
        std::fprintf(f, "\n");
    }
    std::fclose(f);
}

// sub_628410 (from the first device's ctor, sub_634B50): the table's initial
// data.
void PCInputCreateDevice() {
    std::memcpy(gGameActions, kDefaultGameActions, sizeof(gGameActions));
    gCapturing = false;
    ReadRaw(gRaw);
    gPrevRaw = gRaw;
}

int PCInputGetNumDeviceScalars() {
    return PCINPUT_NUM_ACTIONS;
}

const char *PCInputGetDeviceScalarName(int i) {
    return kDefaultGameActions[i].Name;
}

// sub_63F850 (the device's Initialize): the joystick's rest state, then its
// default bindings. Natively the saved bindings load here too (the PC loads
// them with the profile).
void PCInputInitializeDevice() {
    ReadRaw(gRaw);
    gPrevRaw = gRaw;
    gJoyRest = gRaw.Joy;
    DefaultJoystick();
    extern int IsMemcardEnabled;
    if (!IsMemcardEnabled) {
        LoadBindings(); // with profiles they load with the profile, as on PC
    }
}

// sub_6349B0 (the device's PollDevice): while capturing only the capture runs
// and the scalars keep their values; otherwise each action's value is the
// largest of its bindings'.
void PCInputPollDevice(float *prev, float *current) {
    gPrevRaw = gRaw;
    ReadRaw(gRaw);
    if (gRaw.PadConnected && !gPrevRaw.PadConnected) {
        // A pad that connects after start-up: the PC only sees joysticks
        // there at start-up; natively its rest state and, if it is not the
        // joystick the bindings were for, its defaults. TODO(port): addition.
        gJoyRest = gRaw.Joy;
        if (JoystickIdentity() != gJoystickIdentity) {
            DefaultJoystick();
        }
    }
    if (gCapturing) {
        PollCapture();
        return;
    }
    std::memcpy(prev, current, sizeof(float) * PCINPUT_NUM_ACTIONS);
    for (int i = 0; i < PCINPUT_NUM_ACTIONS; i++) {
        float value = 0.0f;
        for (int s = 0; s < 4; s++) {
            const PCInputBinding &b = gGameActions[i].Bindings[s];
            if (b.Type != PCINPUT_UNBOUND) {
                const float v = BindingValue(b);
                if (v >= value) {
                    value = v;
                }
            }
        }
        current[i] = value;
    }
}
