#include "ENISCarShake.hpp"
#include "Speed/Indep/Src/Interfaces/Simables/INISCarControl.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Main/EventSequencer.h"

// PC: ENISCarShake 0x639890, ENISCarShake_MakeEvent_Callback 0x639980. Fired by an NIS's event sequence
// for one of its cars (the simable from the event's dynamic data): the car body shakes (INISCarControl::SetAnimShake).
ENISCarShake::ENISCarShake(float pShake, float pRate, float pRamp, float pTime, uintptr_t phSimable)
    : Event(0x20), //
      fShake(pShake), //
      fRate(pRate), //
      fRamp(pRamp), //
      fTime(pTime), //
      fhSimable(phSimable) {
    if (fhSimable != 0) {
        ISimable *simable = ISimable::FindInstance(reinterpret_cast<HSIMABLE>(fhSimable));
        INISCarControl *iface;
        if (simable != nullptr && simable->QueryInterface(&iface)) {
            iface->SetAnimShake(fShake, fRate, fRamp, fTime);
        }
    }
}

ENISCarShake::~ENISCarShake() {}

const char *ENISCarShake::GetEventName() {
    return "ENISCarShake";
}

void ENISCarShake_MakeEvent_Callback(const void *staticData) {
    const ENISCarShake::StaticData *data = static_cast<const ENISCarShake::StaticData *>(staticData);
    new ENISCarShake(data->fShake, data->fRate, data->fRamp, data->fTime, gEventDynamicData.fhSimable);
}
