#ifndef INPUT_PCINPUTBINDINGS_H
#define INPUT_PCINPUTBINDINGS_H

#ifdef EA_PRAGMA_ONCE_SUPPORTED
#pragma once
#endif

// The PC (speed.exe 1.3) input device's action table and the binding API the
// controller configuration screen (UIOptionsPCController) uses. The PC device
// (vtable 0x8A7B9C) exposes one device scalar per action of the table
// (GameAction @ 0x91F418, initial data 0x8F6D80); an action's value is the
// largest of its four bindings: keyboard primary and secondary, joystick
// primary and secondary. The first PCINPUT_NUM_CONFIG_ACTIONS actions are the
// ones the screen rebinds and the profile saves.
//
// The PC IOModule and its DirectInput devices are not decompiled; natively
// this is implemented by port/src/Platform/InputBindingsNative.cpp.

#include <types.h>

enum PCInputDeviceType {
    PCINPUT_KEYBOARD = 1,
    PCINPUT_JOYSTICK = 3,
};

enum PCInputBindingType {
    PCINPUT_UNBOUND = 0,
    PCINPUT_KEY = 1,        // Code: DirectInput scan code
    PCINPUT_BUTTON = 2,     // Code: joystick, Index: button
    PCINPUT_POV = 3,        // Code: joystick, Index: 0 up, 1 right, 2 down, 3 left
    PCINPUT_AXIS_LOW = 4,   // Code: joystick, Index: 0 X, 1 Y, 2 Z; towards 0
    PCINPUT_AXIS_HIGH = 5,  // ... towards 65535
    PCINPUT_ROT_LOW = 6,    // Index: 0 Rx, 1 Ry, 2 Rz
    PCINPUT_ROT_HIGH = 7,
    PCINPUT_SLIDER_LOW = 8, // Index: slider
    PCINPUT_SLIDER_HIGH = 9,
};

enum {
    PCINPUT_NUM_ACTIONS = 76,
    PCINPUT_NUM_CONFIG_ACTIONS = 19,
    PCINPUT_NUM_KEYBOARD_CONFIG_ACTIONS = 18, // the keyboard page has no HUDACTION_PAUSEREQUEST
    PCINPUT_BINDING_NAME_SIZE = 0x104,
};

// Binding slots of an action: 0/1 keyboard primary/secondary, 2/3 joystick.
// size: 0xC
struct PCInputBinding {
    int Type;  // PCInputBindingType
    int Code;  // keyboard: scan code; joystick: joystick number
    int Index; // joystick: button / POV direction / axis
};

// sub_628CB0: the four binding slots of an action.
PCInputBinding *PCInputGetBindings(int action);
// sub_627BB0: bStringHash of the action's device scalar name (its label).
uint32 PCInputGetActionNameHash(int action);
// sub_628230: the bound input's name ("---" when unbound).
void PCInputGetBindingName(char *name, int device, int action, bool primary);
// sub_628560: clears a binding; clearing the primary moves the secondary up.
void PCInputClearBinding(int device, int action, bool primary);
// sub_6284D0: the keyboard bindings' defaults.
void PCInputDefaultKeyboardBindings();
// sub_63CD80: the joystick bindings' defaults for the attached joystick.
void PCInputDefaultJoystickBindings();
// sub_628C00: the next key / joystick input becomes the binding.
void PCInputStartCapture(int device, int action, bool primary);
// sub_628CA0
bool PCInputIsCapturing();
// sub_628C50
bool PCInputJoystickPresent();
// sub_628C60: the device's DirectInput product name.
const char *PCInputGetDeviceName(int device);

#ifdef EA_PORT_NATIVE
// The PC saves the bindings in the user profile (UserProfile::SaveToBuffer
// 0x58FD10 -> sub_56E180, LoadFromBuffer -> sub_56E200): the joystick's
// identity, then the rebindable actions' four bindings each.
enum {
    PCINPUT_PROFILE_BINDINGS_SIZE = 4 + PCINPUT_NUM_CONFIG_ACTIONS * 4 * 0xC,
};
char *PCInputSaveBindingsToBuffer(char *buf, char *maxbuf);
char *PCInputLoadBindingsFromBuffer(char *buf, char *maxbuf);
// Without profiles (NFSMW_MEMCARD=0) the port keeps them in their own file.
void PCInputSaveBindings();
#endif

#endif
