#include "ENISBurnout.hpp"
#include "Speed/Indep/Src/Interfaces/Simables/INISCarControl.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Main/EventSequencer.h"

// PC: ENISBurnout 0x639190, ENISBurnout_MakeEvent_Callback 0x639260. Fired by an NIS's event sequence
// for one of its cars (the simable from the event's dynamic data): the car spins its wheels at the speed (INISCarControl::SetBurnout).
ENISBurnout::ENISBurnout(float pSpeed, uintptr_t phSimable)
    : Event(0x10), //
      fSpeed(pSpeed), //
      fhSimable(phSimable) {
    if (fhSimable != 0) {
        ISimable *simable = ISimable::FindInstance(reinterpret_cast<HSIMABLE>(fhSimable));
        INISCarControl *iface;
        if (simable != nullptr && simable->QueryInterface(&iface)) {
            iface->SetBurnout(fSpeed);
        }
    }
}

ENISBurnout::~ENISBurnout() {}

const char *ENISBurnout::GetEventName() {
    return "ENISBurnout";
}

void ENISBurnout_MakeEvent_Callback(const void *staticData) {
    const ENISBurnout::StaticData *data = static_cast<const ENISBurnout::StaticData *>(staticData);
    new ENISBurnout(data->fSpeed, gEventDynamicData.fhSimable);
}
