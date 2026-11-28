#include "ENISNeutralRev.hpp"
#include "Speed/Indep/Src/Interfaces/Simables/INISCarControl.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Main/EventSequencer.h"

// PC: ENISNeutralRev 0x639F20, ENISNeutralRev_MakeEvent_Callback 0x63A030. Fired by an NIS's event sequence
// for one of its cars (the simable from the event's dynamic data): the car revs in neutral (INISCarEngine::SetNeutralRev).
ENISNeutralRev::ENISNeutralRev(unsigned int pOn, float pThrottle, float pSpeed, uintptr_t phSimable)
    : Event(0x20), //
      fOn(pOn), //
      fThrottle(pThrottle), //
      fSpeed(pSpeed), //
      fhSimable(phSimable) {
    if (fhSimable != 0) {
        ISimable *simable = ISimable::FindInstance(reinterpret_cast<HSIMABLE>(fhSimable));
        INISCarEngine *iface;
        if (simable != nullptr && simable->QueryInterface(&iface)) {
            if (fOn == 0) {
                iface->SetNeutralRev(false, 0.0f, 0.0f);
            } else {
                iface->SetNeutralRev(true, fThrottle, fSpeed);
            }
        }
    }
}

ENISNeutralRev::~ENISNeutralRev() {}

const char *ENISNeutralRev::GetEventName() {
    return "ENISNeutralRev";
}

void ENISNeutralRev_MakeEvent_Callback(const void *staticData) {
    const ENISNeutralRev::StaticData *data = static_cast<const ENISNeutralRev::StaticData *>(staticData);
    new ENISNeutralRev(data->fOn, data->fThrottle, data->fSpeed, gEventDynamicData.fhSimable);
}
