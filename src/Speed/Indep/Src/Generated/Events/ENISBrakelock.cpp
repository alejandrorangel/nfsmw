#include "ENISBrakelock.hpp"
#include "Speed/Indep/Src/Interfaces/Simables/INISCarControl.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Main/EventSequencer.h"

// PC: ENISBrakelock 0x638FB0, ENISBrakelock_MakeEvent_Callback 0x639090. Fired by an NIS's event sequence
// for one of its cars (the simable from the event's dynamic data): the car's front/rear brakes lock (INISCarControl::SetBrakeLock).
ENISBrakelock::ENISBrakelock(unsigned int pFront, unsigned int pRear, uintptr_t phSimable)
    : Event(0x20), //
      fFront(pFront), //
      fRear(pRear), //
      fhSimable(phSimable) {
    if (fhSimable != 0) {
        ISimable *simable = ISimable::FindInstance(reinterpret_cast<HSIMABLE>(fhSimable));
        INISCarControl *iface;
        if (simable != nullptr && simable->QueryInterface(&iface)) {
            iface->SetBrakeLock(fFront != 0, fRear != 0);
        }
    }
}

ENISBrakelock::~ENISBrakelock() {}

const char *ENISBrakelock::GetEventName() {
    return "ENISBrakelock";
}

void ENISBrakelock_MakeEvent_Callback(const void *staticData) {
    const ENISBrakelock::StaticData *data = static_cast<const ENISBrakelock::StaticData *>(staticData);
    new ENISBrakelock(data->fFront, data->fRear, gEventDynamicData.fhSimable);
}
