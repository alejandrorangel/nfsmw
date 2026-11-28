#include "ENISNitro.hpp"
#include "Speed/Indep/Src/Interfaces/Simables/INISCarControl.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Main/EventSequencer.h"

// PC: ENISNitro 0x63A140, ENISNitro_MakeEvent_Callback 0x63A230. Fired by an NIS's event sequence
// for one of its cars (the simable from the event's dynamic data): the car's nitro on or off (INISCarEngine::SetNitro).
ENISNitro::ENISNitro(unsigned int pOn, uintptr_t phSimable)
    : Event(0x10), //
      fOn(pOn), //
      fhSimable(phSimable) {
    if (fhSimable != 0) {
        ISimable *simable = ISimable::FindInstance(reinterpret_cast<HSIMABLE>(fhSimable));
        INISCarEngine *iface;
        if (simable != nullptr && simable->QueryInterface(&iface)) {
            iface->SetNitro(fOn != 0);
        }
    }
}

ENISNitro::~ENISNitro() {}

const char *ENISNitro::GetEventName() {
    return "ENISNitro";
}

void ENISNitro_MakeEvent_Callback(const void *staticData) {
    const ENISNitro::StaticData *data = static_cast<const ENISNitro::StaticData *>(staticData);
    new ENISNitro(data->fOn, gEventDynamicData.fhSimable);
}
