// Native input: IOModule and one InputDevice that exposes keyboard and
// gamepad controls under the device-scalar names the PC controller configs
// (attribute class "controller") bind actions to. Both the PC IOModule and its
// DirectInput devices are undecompiled (Input/Common/iomodule.cpp is empty).
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

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Same Jenkins hash (seed 0xABCDEF00) as Attrib::StringHash32 in the PC build
// (stringhash32 @ 0x5CC240, Attrib::StringHash32 @ 0x4519D0).
unsigned int stringhash32(const char *k) {
    return Attrib::StringHash32(k);
}

namespace {

// What a device scalar reads from the platform snapshot.
struct ScalarSource {
    const char *name;
    DeviceScalarType type;
    enum { KEY, PAD, PAD_NEG, PAD_POS } kind;
    int index;
};

#define KEY(n, k) {n, kDigitalButton, ScalarSource::KEY, PORT_KEY_##k}
#define PAD(n, p) {n, kAnalogButton, ScalarSource::PAD, PORT_PAD_##p}
#define AXIS_NEG(n, p) {n, kJoyAxis, ScalarSource::PAD_NEG, PORT_PAD_##p}
#define AXIS_POS(n, p) {n, kJoyAxis, ScalarSource::PAD_POS, PORT_PAD_##p}

const ScalarSource kScalars[] = {
    // Keyboard: DirectInput scan-code names (DIK_* without the prefix).
    KEY("SC_ESCAPE", ESCAPE), KEY("SC_RETURN", ENTER), KEY("SC_SPACE", SPACE), KEY("SC_BACK", BACKSPACE), KEY("SC_TAB", TAB),
    KEY("SC_UP", UP), KEY("SC_DOWN", DOWN), KEY("SC_LEFT", LEFT), KEY("SC_RIGHT", RIGHT),
    KEY("SC_PAGEUP", PAGEUP), KEY("SC_PAGEDOWN", PAGEDOWN), KEY("SC_INSERT", INSERT), KEY("SC_DELETE", DELETE),
    KEY("SC_HOME", HOME), KEY("SC_END", END),
    KEY("SC_LSHIFT", LSHIFT), KEY("SC_RSHIFT", RSHIFT), KEY("SC_LCONTROL", LCONTROL), KEY("SC_RCONTROL", RCONTROL),
    KEY("SC_LALT", LALT), KEY("SC_RALT", RALT),
    KEY("SC_MINUS", MINUS), KEY("SC_EQUALS", EQUALS), KEY("SC_LBRACKET", LBRACKET), KEY("SC_RBRACKET", RBRACKET),
    KEY("SC_BACKSLASH", BACKSLASH), KEY("SC_SEMICOLON", SEMICOLON), KEY("SC_APOSTROPHE", APOSTROPHE),
    KEY("SC_COMMA", COMMA), KEY("SC_PERIOD", PERIOD), KEY("SC_SLASH", SLASH), KEY("SC_GRAVE", GRAVE),
    KEY("SC_F1", F1), KEY("SC_F2", F2), KEY("SC_F3", F3), KEY("SC_F4", F4), KEY("SC_F5", F5), KEY("SC_F6", F6),
    KEY("SC_F7", F7), KEY("SC_F8", F8), KEY("SC_F9", F9), KEY("SC_F10", F10), KEY("SC_F11", F11), KEY("SC_F12", F12),
    KEY("SC_NUMPAD0", NUMPAD0), KEY("SC_NUMPAD1", NUMPAD1), KEY("SC_NUMPAD2", NUMPAD2), KEY("SC_NUMPAD3", NUMPAD3),
    KEY("SC_NUMPAD4", NUMPAD4), KEY("SC_NUMPAD5", NUMPAD5), KEY("SC_NUMPAD6", NUMPAD6), KEY("SC_NUMPAD7", NUMPAD7),
    KEY("SC_NUMPAD8", NUMPAD8), KEY("SC_NUMPAD9", NUMPAD9), KEY("SC_NUMPADENTER", NUMPADENTER),
    KEY("SC_ADD", ADD), KEY("SC_SUBTRACT", SUBTRACT), KEY("SC_MULTIPLY", MULTIPLY), KEY("SC_DIVIDE", DIVIDE), KEY("SC_DECIMAL", DECIMAL),
    KEY("SC_A", A), {"SC_B", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 1}, {"SC_C", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 2},
    {"SC_D", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 3}, {"SC_E", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 4},
    {"SC_F", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 5}, {"SC_G", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 6},
    {"SC_H", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 7}, {"SC_I", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 8},
    {"SC_J", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 9}, {"SC_K", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 10},
    {"SC_L", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 11}, {"SC_M", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 12},
    {"SC_N", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 13}, {"SC_O", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 14},
    {"SC_P", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 15}, {"SC_Q", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 16},
    {"SC_R", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 17}, {"SC_S", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 18},
    {"SC_T", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 19}, {"SC_U", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 20},
    {"SC_V", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 21}, {"SC_W", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 22},
    {"SC_X", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 23}, {"SC_Y", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 24},
    {"SC_Z", kDigitalButton, ScalarSource::KEY, PORT_KEY_A + 25},
    {"SC_0", kDigitalButton, ScalarSource::KEY, PORT_KEY_0 + 0}, {"SC_1", kDigitalButton, ScalarSource::KEY, PORT_KEY_0 + 1},
    {"SC_2", kDigitalButton, ScalarSource::KEY, PORT_KEY_0 + 2}, {"SC_3", kDigitalButton, ScalarSource::KEY, PORT_KEY_0 + 3},
    {"SC_4", kDigitalButton, ScalarSource::KEY, PORT_KEY_0 + 4}, {"SC_5", kDigitalButton, ScalarSource::KEY, PORT_KEY_0 + 5},
    {"SC_6", kDigitalButton, ScalarSource::KEY, PORT_KEY_0 + 6}, {"SC_7", kDigitalButton, ScalarSource::KEY, PORT_KEY_0 + 7},
    {"SC_8", kDigitalButton, ScalarSource::KEY, PORT_KEY_0 + 8}, {"SC_9", kDigitalButton, ScalarSource::KEY, PORT_KEY_0 + 9},

    // Gamepad: Xbox-style names used by the controller configs. Sticks are
    // split into half-axes (ALX_L = left stick pushed left, ...).
    PAD("A", A), PAD("B", B), PAD("X", X), PAD("Y", Y), PAD("White", LSHOULDER), PAD("Black", RSHOULDER),
    PAD("LTrig", LTRIGGER), PAD("RTrig", RTRIGGER), PAD("Start", START), PAD("Back", BACK),
    PAD("LThumb", LSTICK), PAD("RThumb", RSTICK),
    PAD("DUp", DUP), PAD("DDown", DDOWN), PAD("DLeft", DLEFT), PAD("DRight", DRIGHT),
    AXIS_NEG("ALX_L", LX), AXIS_POS("ALX_R", LX), AXIS_POS("ALY_U", LY), AXIS_NEG("ALY_D", LY),
    AXIS_NEG("ARX_L", RX), AXIS_POS("ARX_R", RX), AXIS_POS("ARY_U", RY), AXIS_NEG("ARY_D", RY),
};

const int kNumScalars = sizeof(kScalars) / sizeof(kScalars[0]);

class MacInputDevice : public InputDevice {
  public:
    explicit MacInputDevice(int index) : InputDevice(index) {
        this->fDeviceScalar = new DeviceScalar[kNumScalars];
        this->fPrevValues = new float[kNumScalars]();
        this->fCurrentValues = new float[kNumScalars]();
        for (int i = 0; i < kNumScalars; i++) {
            this->fDeviceScalar[i].InitializeDeviceScalar(kScalars[i].type, kScalars[i].name, &this->fPrevValues[i], &this->fCurrentValues[i]);
        }
    }

    bool IsConnected() override {
        return true;
    }
    void Initialize() override {}
    int GetNumDeviceScalar() override {
        return kNumScalars;
    }
    void StartVibration() override {}
    void StopVibration() override {}

    void PollDevice() override {
        PortInputState state;
        PortInputGetState(&state);
        for (int i = 0; i < kNumScalars; i++) {
            const ScalarSource &s = kScalars[i];
            float v = 0.0f;
            switch (s.kind) {
            case ScalarSource::KEY:
                v = state.keys[s.index];
                break;
            case ScalarSource::PAD:
                v = state.pad[s.index];
                break;
            case ScalarSource::PAD_NEG:
                v = state.pad[s.index] < 0.0f ? -state.pad[s.index] : 0.0f;
                break;
            case ScalarSource::PAD_POS:
                v = state.pad[s.index] > 0.0f ? state.pad[s.index] : 0.0f;
                break;
            }
            this->fPrevValues[i] = this->fCurrentValues[i];
            this->fCurrentValues[i] = v;
        }
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
            for (unsigned int i = 0; i < attribute.GetLength(); i++) {
                const ControllerDataRecord &rec = attribute.Get<ControllerDataRecord>(i);
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
}

void IOModule::Update() {
    if (fUpdateEnabled) {
        PollDevices();
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
