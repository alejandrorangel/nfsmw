#include <cstdio>
#include <cstdlib>
#include "Speed/Indep/Src/Input/ActionQueue.h"
#include "Speed/Indep/Libs/Support/Utility/UListable.h"
#include "Speed/Indep/Libs/Support/Utility/UStandard.h"
#include "Speed/Indep/Src/Generated/AttribSys/Classes/controller.h"
#include "Speed/Indep/Src/Input/Action.h"
#include "Speed/Indep/Src/Input/ActionData.h"
#include "Speed/Indep/Src/Input/ActionRef.h"
#include "Speed/Indep/Src/Input/IOModule.h"
#include "Speed/Indep/Src/Input/InputDefParser.h"
#include "Speed/Indep/Src/Input/InputDevice.h"
#include "Speed/Indep/Src/Main/AttribSupport.h"
#include "Speed/Indep/Src/Misc/Joylog.hpp"
#include "Speed/Indep/Src/Misc/Timer.hpp"
#include "Speed/Indep/Tools/AttribSys/Runtime/Common/AttribPrivate.h"

static const int PrintActionQueue = 0;

bool ActionQueue::sInJoylogFrame = false;
Timer ActionQueue::mLastAnyActionTime;

IMPLEMENT_LISTABLE(ActionQueue)

ActionQueue::ActionQueue(bool required) {
    this->mRequired = required;
    this->mPort = -1;
    this->mMappings = nullptr;
    this->mConfig = 0;
    this->mActionTime = Timer();
    this->mActivationTime = Timer();
    this->mState = AQS_ENABLED;
    this->mUniqueID = this->AssignUniqueID();
    this->mQueueName = "Unnamed";
    this->mConnected = false;

    this->fQueue.reset();
}

ActionQueue::ActionQueue(int port, unsigned int config, const char *queue_name, bool required) {
    this->mRequired = required;
    this->mPort = -1;
    this->mMappings = nullptr;
    this->mConfig = 0;
    this->mActionTime = Timer();
    this->mActivationTime = Timer();
    this->mState = AQS_ENABLED;
    this->mUniqueID = this->AssignUniqueID();
    this->mConnected = false;

    this->fQueue.reset();
    this->mPort = port;

    this->SetConfig(config, queue_name);
}

ActionQueue::~ActionQueue() {
    if (this->mMappings != nullptr) {
        delete this->mMappings;
    }
}

// const ActionRef ActionQueue::operator[](int i) {

// }

bool ActionQueue::IsConnected() const {
    if (this->mPort < 0) {
        return false;
    }
    return this->mConnected;
}

void ActionQueue::IO_SetConnected(bool plugged) {
    if (this->mPort < 0) {
        return;
    }
    if (this->mConnected == plugged) {
        return;
    }
    if (plugged) {
        ActionData a(ACTION_PLUGGED, 0.0f, this->mPort);

        this->ReceiveAction(a);
    } else {
        ActionData a(ACTION_UNPLUGGED, 0.0f, this->mPort);

        this->ReceiveAction(a);
    }
}

void ActionQueue::IO_Flush() {
    if (this->mPort < 0) {
        return;
    }

    ActionData a(ACTION_FLUSH, 0.0f, this->mPort);

    this->ReceiveAction(a);
}

void ActionQueue::Init(int port, unsigned int config) {
    if (port == this->mPort && config == this->mConfig) {
        return;
    }
    this->mConfig = config;
    this->mPort = port;

    if (this->mMappings != nullptr) {
        delete this->mMappings;
        this->mMappings = nullptr;
    }

    if (port >= 0) {
        InputDevice *device = IOModule::GetIOModule().GetDevice(port);
        if (device != nullptr) {
            const Attrib::Collection *spec = Attrib::FindCollection(Attrib::Gen::controller::ClassKey(), this->mConfig);
            if (spec != nullptr) {
                this->mMappings = new InputMapping(device, spec);
            }
        }
    }

    this->OnActivationChange();
}

void ActionQueue::SetPort(int port) {
    this->Init(port, this->mConfig);
}

void ActionQueue::SetConfig(unsigned int config, const char *queue_name) {
    this->mQueueName = queue_name;
    this->Init(this->mPort, config);
}

bool ActionQueue::IsEnabled() const {
    return (this->mState < AQS_ENABLED) ? false : true;
}

void ActionQueue::Enable(bool b) {
    if (b) {
        if (this->mState == AQS_DISABLED) {
            this->mState = AQS_ENABLED;
            this->Flush();
        }
    } else {
        if (this->mState != AQS_DISABLED) {
            this->mState = AQS_DISABLED;
            this->Flush();
        }
    }

    this->OnActivationChange();
}

// unfinished
#if defined(EA_PORT_NATIVE)
// PC (speed.exe 1.3 @ 0x642C90): each mapping's current value from its device
// scalar, by update type: presses are 0/1 at 0.5 (analog ones at the lower
// dead zone), plain updates ramp between the dead zones; -1 if unmapped.
void ActionQueue::FetchCurrentValues(InputDevice *device) {
    UTL::Std::list<InputMapEntry, _type_list> &e = this->mMappings->GetEntries();
    for (UTL::Std::list<InputMapEntry, _type_list>::iterator iter = e.begin(); iter != e.end(); iter++) {
        InputMapEntry &entry = *iter;
        if (entry.DeviceScalarIndex < 0 || entry.DeviceScalarIndex >= device->GetNumDeviceScalar()) {
            entry.PreviousValue = -1.0f;
            entry.CurrentValue = -1.0f;
            continue;
        }
        float value = device->GetDeviceScalar(entry.DeviceScalarIndex)->GetValue();
        switch (entry.UpdateType) {
            case kPress:
            case kRelease:
                entry.CurrentValue = value >= 0.5f ? 1.0f : 0.0f;
                break;
            case kAnalogPress:
            case kAnalogRelease:
                entry.CurrentValue = value >= entry.LowerDZ ? 1.0f : 0.0f;
                break;
            default:
                entry.CurrentValue = UMath::Ramp(value, entry.LowerDZ, 1.0f - entry.UpperDZ);
                break;
        }
    }
}
#else
void ActionQueue::FetchCurrentValues(InputDevice *device) {
    UTL::Std::list<InputMapEntry, _type_list> &e = this->mMappings->GetEntries();

    for (UTL::Std::list<InputMapEntry, _type_list>::iterator iter = e.begin(); iter != e.end(); iter++) {
        if (iter->DeviceScalarIndex != 0) {
            InputMapEntry &entry = *iter;
            DeviceScalar *button = device->GetDeviceScalar(0);

            // UMath::Ramp(button->GetValue(), button->GetValue(), button->GetValue());
        }
    }
}

#endif

ActionQueue *ActionQueue::FindActionQueue(int id) {
    for (ActionQueue *const *iter = ActionQueue::GetList().begin(); iter != ActionQueue::GetList().end(); iter++) {
        ActionQueue *q = *iter;

        if (q->mUniqueID == id) {
            return q;
        }
    }

    return nullptr;
}

// ActionQueue::FindActionQueue(unsigned int config, int port)

int ActionQueue::AssignUniqueID() {
    int id = 0;
    while (ActionQueue::FindActionQueue(id) != nullptr) {
        id += 1;
    }

    return id;
}

#if defined(EA_PORT_NATIVE)
// PC (speed.exe 1.3 @ 0x6448E0): queue an action when a mapping's value
// changes: always for plain updates, on the rising edge (>= 0.5) for presses,
// on the falling edge for releases. When several mappings drive one action,
// the one with the larger value wins.
void ActionQueue::IO_UpdateFromDevice() {
    static const bool trace = std::getenv("NFSMW_TRACE_ACTIONS") != nullptr;
    if (trace) {
        static int n;
        if (n++ % 600 == 0) {
            std::fprintf(stderr, "[actions] queue %d port %d state %d mappings %p (%d entries)\n", mUniqueID, mPort, mState, mMappings,
                         mMappings ? (int)mMappings->GetEntries().size() : -1);
        }
    }
    if (this->mMappings == nullptr || this->mState == AQS_DISABLED || this->mPort < 0) {
        return;
    }
    InputDevice *device = IOModule::GetIOModule().GetDevice(this->mPort);
    if (device == nullptr) {
        return;
    }
    this->FetchCurrentValues(device);

    UTL::Std::list<InputMapEntry, _type_list> &e = this->mMappings->GetEntries();
    for (UTL::Std::list<InputMapEntry, _type_list>::iterator it = e.begin(); it != e.end(); it++) {
        InputMapEntry &entry = *it;
        float current = entry.CurrentValue;
        float previous = entry.PreviousValue;
        entry.PreviousValue = current;
        if (current < 0.0f) {
            continue;
        }
        float change = current - previous;
        if (change < 0.0f) {
            change = -change;
        }
        if (change <= 9.999999974752427e-07f) {
            continue;
        }
        if (entry.UpdateType == kUpdate) {
            bool overridden = false;
            for (UTL::Std::list<InputMapEntry, _type_list>::iterator other = e.begin(); other != e.end(); other++) {
                if (&*other != &entry && other->Action == entry.Action && other->UpdateType == kUpdate && other->CurrentValue > current) {
                    overridden = true;
                    break;
                }
            }
            if (overridden) {
                continue;
            }
        }
        bool send = false;
        switch (entry.UpdateType) {
            case kPress:
            case kAnalogPress:
                send = current >= 0.5f && previous < 0.5f;
                break;
            case kRelease:
            case kAnalogRelease:
                send = current < 0.5f && previous >= 0.5f && previous >= 0.0f;
                break;
            default:
                send = true;
                break;
        }
        if (send && entry.Action != 0) {
            if (trace) {
                std::fprintf(stderr, "[actions] queue %d: %s %.2f\n", mUniqueID, getActionIdString(entry.Action), current);
            }
            ActionData action(entry.Action, current, this->mPort);
            this->ReceiveAction(action);
        }
    }
}

void ActionQueue::PortUpdateAllFromDevices() {
    for (ActionQueue *const *iter = ActionQueue::GetList().begin(); iter != ActionQueue::GetList().end(); iter++) {
        ActionQueue *q = *iter;
        // Plug state from the device (queues ACTION_PLUGGED / ACTION_UNPLUGGED).
        if (q->mPort >= 0) {
            InputDevice *device = IOModule::GetIOModule().GetDevice(q->mPort);
            q->IO_SetConnected(device != nullptr && device->IsConnected());
        }
        q->IO_UpdateFromDevice();
    }
}
#else
// unfinished
void ActionQueue::IO_UpdateFromDevice() {
    if (this->mMappings == nullptr || this->mState == AQS_DISABLED || this->mPort < 0)
        return;

    InputDevice *device = IOModule::GetIOModule().GetDevice(this->mPort);
    ActionData ad = ActionData(0, 0.0f, 0);

    this->FetchCurrentValues(device);

    UTL::Std::list<InputMapEntry, _type_list> &e = this->mMappings->GetEntries();
    for (UTL::Std::list<InputMapEntry, _type_list>::iterator iaction = e.begin(); iaction != e.end(); iaction++) {
        InputMapEntry &entry = *iaction;
        if (entry.HasChanged()) {
            iaction++;
            continue;
        }
    }
}

#endif

void ActionQueue::OnActivationChange() {
    bool is_active;
    bool was_active = false;

    if (this->IsEnabled() && this->IsConnected()) {
        is_active = this->IsActive();
        if (is_active) {
            was_active = true;
        }
    }
    if (was_active != (this->mActivationTime.IsSet() != 0)) {
        if (was_active) {
            this->mActivationTime = RealTimer;
        } else {
            this->mActivationTime.ResetLow();
        }
    }
}

bool ActionQueue::IsEmpty() {
    return this->fQueue.size() == 0;
}

// unfinished
bool ActionQueue::ReceiveAction(ActionData &action) {
    if (action.Slot() != this->mPort) {
        return false;
    }
    if (action.ID() != ACTION_UNPLUGGED) {
        this->mLastAnyActionTime = RealTimer;
    }
    Joylog::AddData(this->mUniqueID, 8, JOYLOG_CHANNEL_JOYEVENTS);
    Joylog::AddData(action.ID(), 8, JOYLOG_CHANNEL_JOYEVENTS);
    Joylog::AddData(action.Data(), JOYLOG_CHANNEL_JOYEVENTS);

    if (action.ID() == ACTION_UNPLUGGED) {
        this->Flush();
        this->mConnected = false;
        this->OnActivationChange();
    } else if (action.ID() == ACTION_PLUGGED) {
        this->Flush();
        this->mConnected = true;
        this->OnActivationChange();
    } else if (action.ID() == ACTION_FLUSH) {
        this->Flush();
    }
    this->mActionTime = RealTimer;

    this->fQueue.size(); // dwarf nonsense
    this->fQueue.enqueue(action);

    return true;
}

void ActionQueue::PopAction() {
    if (this->fQueue.size() > 0) {
        this->fQueue.dequeue();
    }
}

void ActionQueue::Flush() {
    this->fQueue.reset();

    if (this->mMappings != nullptr) {
        UTL::Std::list<InputMapEntry, _type_list> &e = this->mMappings->GetEntries();

        for (UTL::Std::list<InputMapEntry, _type_list>::iterator iter = e.begin(); iter != e.end(); iter++) {
            InputMapEntry &entry = *iter;
            entry.PreviousValue = -1.0;
            entry.CurrentValue = -1.0;
        }
    }
}

const ActionRef ActionQueue::GetAction() {
    if (this->fQueue.size() > 0) {
        ActionData *a = &this->fQueue.tail();
        return ActionRef(a);
    } else {
        return ActionRef(nullptr);
    }
}

void ActionQueue::BeginJoylogFrame() {
    sInJoylogFrame = true;
    if (Joylog::IsReplaying()) {
        while (true) {
            int queue_id = Joylog::GetSignedData(0x8, JOYLOG_CHANNEL_JOYEVENTS);
            if (queue_id == -1)
                return;

            ActionQueue *q = ActionQueue::FindActionQueue(queue_id);
            int action_id = Joylog::GetData(0x8, JOYLOG_CHANNEL_JOYEVENTS);
            float data = Joylog::GetData(JOYLOG_CHANNEL_JOYEVENTS);
            ActionData a = ActionData(action_id, data, q->mPort);
            q->ReceiveAction(a);
        }
    }
}

void ActionQueue::EndJoylogFrame() {
    if (!Joylog::IsReplaying()) {
        Joylog::AddData(-1, 0x8, JOYLOG_CHANNEL_JOYEVENTS);
    }
    sInJoylogFrame = false;
}
