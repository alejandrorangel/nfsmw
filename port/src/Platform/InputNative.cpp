// Native input: IOModule and the PC input device, whose scalars are the
// device-scalar names the PC controller configs (attribute class
// "controller") bind actions to. Both the PC IOModule and its DirectInput
// devices are undecompiled (Input/Common/iomodule.cpp is empty).
//
// NFSMW_DUMP_CONTROLLERS=1 prints every config's action -> device bindings.

#include "Speed/Indep/Src/Generated/AttribSys/Classes/controller.h"
#include "Speed/Indep/Src/Input/Action.h"
#include "Speed/Indep/Src/Input/IOModule.h"
#include "Speed/Indep/Src/Input/InputDevice.h"
#include "Speed/Indep/Src/Main/AttribSupport.h"
#include "Speed/Indep/Src/Misc/MWAttribUserTypes.h"
#include "Speed/Indep/Src/Misc/attribuserinclude.h"
#include "Speed/Indep/Tools/AttribSys/Runtime/AttribHash.h"
#include "Speed/Indep/Tools/AttribSys/Runtime/AttribSys.h"

#include "../Mac/InputBridge.h"
#include "InputBindingsNative.h"
#include "Speed/Indep/Src/FEng/FEMouse.h"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngMouse.hpp"


#include <cstdio>
#include <cstdlib>
#include <cstring>

// Same Jenkins hash (seed 0xABCDEF00) as Attrib::StringHash32 in the PC build
// (stringhash32 @ 0x5CC240, Attrib::StringHash32 @ 0x4519D0).
unsigned int stringhash32(const char *k) {
    return Attrib::StringHash32(k);
}

namespace {

// The PC input device (vtable 0x8A7B9C): one scalar per action of the PC
// action table, valued from its rebindable keyboard / joystick bindings
// (InputBindingsNative.cpp).
class MacInputDevice : public InputDevice {
  public:
    explicit MacInputDevice(int index) : InputDevice(index) {
        PCInputCreateDevice();
        const int n = PCInputGetNumDeviceScalars();
        this->fDeviceScalar = new DeviceScalar[n];
        this->fPrevValues = new float[n]();
        this->fCurrentValues = new float[n]();
        for (int i = 0; i < n; i++) {
            this->fDeviceScalar[i].InitializeDeviceScalar(kAnalogButton, PCInputGetDeviceScalarName(i), &this->fPrevValues[i],
                                                          &this->fCurrentValues[i]);
        }
    }

    bool IsConnected() override {
        return true;
    }
    void Initialize() override {
        PCInputInitializeDevice();
    }
    int GetNumDeviceScalar() override {
        return PCInputGetNumDeviceScalars();
    }
    void StartVibration() override {}
    void StopVibration() override {}

    void PollDevice() override {
        PCInputPollDevice(this->fPrevValues, this->fCurrentValues);
    }
};

void DumpControllerConfigs() {
    Attrib::Class *cls = Attrib::Database::Get().GetClass(Attrib::Gen::controller::ClassKey());
    if (cls == nullptr) {
        std::fprintf(stderr, "[input] no controller class\n");
        return;
    }
    for (Attrib::Key key = cls->GetFirstCollection(); key != 0; key = cls->GetNextCollection(key)) {
        Attrib::Gen::controller atr(Attrib::FindCollection(Attrib::Gen::controller::ClassKey(), key), 0, nullptr);
        const char *name = atr.CollectionName();
        std::fprintf(stderr, "[input] config %#x %s\n", key, name ? name : "?");
        for (int id = 0; id < MAX_ACTIONID; id++) {
            const char *action = getActionIdString(static_cast<ActionID>(id));
            if (action == nullptr) {
                continue;
            }
            Attrib::Attribute attribute = atr.Get(Attrib::StringToKey(action));
            unsigned int length = attribute.GetLength();
            for (unsigned int i = 0; i < length; i++) {
                ControllerDataRecord rec;
                if (!attribute.Get(i, rec)) {
                    std::fprintf(stderr, "[input]   %-32s [%u/%u] unreadable\n", action, i, length);
                    continue;
                }
                if (!rec.mDeviceID.IsEmpty()) {
                    std::fprintf(stderr, "[input]   %-32s %s\n", action, static_cast<const char *>(rec.mDeviceID.GetString()));
                }
            }
        }
    }
}

IOModule *gIOModule;

} // namespace

IOModule &IOModule::GetIOModule() {
    if (gIOModule == nullptr) {
        gIOModule = new IOModule();
    }
    return *gIOModule;
}

IOModule::IOModule() : fUpdateEnabled(true), fNumDevices(0), fDisconnected(0) {
    std::memset(fDevices, 0, sizeof(fDevices));
    CreateDevices();
}

void IOModule::CreateDevices() {
    if (fNumDevices == 0) {
        fDevices[0] = new MacInputDevice(0);
        fNumDevices = 1;
    }
}

void IOModule::Initialize() {
    CreateDevices();
    for (int i = 0; i < fNumDevices; i++) {
        fDevices[i]->Initialize();
    }
    if (std::getenv("NFSMW_DUMP_CONTROLLERS")) {
        DumpControllerConfigs();
    }
}

void IOModule::Release() {}
void IOModule::ReleaseDevices() {}
void IOModule::SetAttributeData() {}

bool IOModule::EnableUpdating(bool enable) {
    bool was = fUpdateEnabled;
    fUpdateEnabled = enable;
    return was;
}

void IOModule::PollDevices() {
    for (int i = 0; i < fNumDevices; i++) {
        fDevices[i]->PollDevice();
    }
}

void IOModule::UpdateAllDevices() {
    PollDevices();
    ActionQueue::PortUpdateAllFromDevices();
}

// PC @ 0x645EC0: poll, then let every action queue read its device.
void IOModule::Update() {
    if (fUpdateEnabled) {
        UpdateAllDevices();
    }
}

bool IOModule::HaveAnyDevicesChanged() {
    for (int i = 0; i < fNumDevices; i++) {
        if (fDevices[i]->DeviceHasChanged()) {
            return true;
        }
    }
    return false;
}

bool IOModule::CheckUnplugged() {
    return false;
}

bool IOModule::MultitapConnected() {
    return false;
}

bool IOModule::IsActiveDevice(int port) {
    return port >= 0 && port < fNumDevices;
}

bool IsJoystickTypeWheel(JoystickPort port) {
    return false;
}

// The PC mouse state (speed.exe 1.3 @ 0x987F50), from Win32 GetCursorPos and
// the DirectInput mouse; here from Cocoa (InputMac.mm).

// 0x6C9B60: the cursor in the 640x480 frontend screen. The PC stretches the
// window's client area over it; the port maps through the renderer's
// aspect-correct placement (PortMetalViewToFEScreen), so in widescreen the
// cursor can leave 0..640. ftol truncates.
void FEMouseState::UpdateCursorPos() {
    PrevX = X;
    PrevY = Y;
    PortInputState state;
    PortInputGetState(&state);
    X = static_cast<int>(state.mouse_x);
    Y = static_cast<int>(state.mouse_y);
    DeltaX = X - PrevX;
    DeltaY = Y - PrevY;
}

// 0x6C9C80, once a frame at the end of eDisplayFrame: buttons 0/1/2 are
// left/right/middle; the wheel is what DirectInput accumulated since last read.
void FEMouseState::Update() {
    UpdateCursorPos();
    Wheel = PortInputTakeMouseWheel();
    PortInputState state;
    PortInputGetState(&state);
    bool left = (state.mouse_buttons & 1) != 0;
    LeftPressed = !LeftDown && left;
    LeftReleased = !left && LeftDown;
    LeftDown = left;
    bool right = (state.mouse_buttons & 2) != 0;
    RightPressed = !RightDown && right;
    RightReleased = !right && RightDown;
    RightDown = right;
    bool middle = (state.mouse_buttons & 4) != 0;
    bool was_middle = MiddleDown;
    MiddlePressed = !was_middle && middle;
    MiddleDown = middle;
    MiddleReleased = !middle && was_middle;
}
