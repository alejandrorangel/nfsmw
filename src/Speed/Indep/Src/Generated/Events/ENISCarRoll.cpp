#include "ENISCarRoll.hpp"
#include "Speed/Indep/Src/Interfaces/Simables/INISCarControl.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Main/EventSequencer.h"

// PC: ENISCarRoll 0x6396B0, ENISCarRoll_MakeEvent_Callback 0x639790. Fired by an NIS's event sequence
// for one of its cars (the simable from the event's dynamic data): the car body rolls (INISCarControl::SetAnimRoll).
ENISCarRoll::ENISCarRoll(float pRoll, float pTime, uintptr_t phSimable)
    : Event(0x20), //
      fRoll(pRoll), //
      fTime(pTime), //
      fhSimable(phSimable) {
    if (fhSimable != 0) {
        ISimable *simable = ISimable::FindInstance(reinterpret_cast<HSIMABLE>(fhSimable));
        INISCarControl *iface;
        if (simable != nullptr && simable->QueryInterface(&iface)) {
            iface->SetAnimRoll(fRoll, fTime);
        }
    }
}

ENISCarRoll::~ENISCarRoll() {}

const char *ENISCarRoll::GetEventName() {
    return "ENISCarRoll";
}

void ENISCarRoll_MakeEvent_Callback(const void *staticData) {
    const ENISCarRoll::StaticData *data = static_cast<const ENISCarRoll::StaticData *>(staticData);
    new ENISCarRoll(data->fRoll, data->fTime, gEventDynamicData.fhSimable);
}
