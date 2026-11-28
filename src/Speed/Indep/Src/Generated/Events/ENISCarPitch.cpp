#include "ENISCarPitch.hpp"
#include "Speed/Indep/Src/Interfaces/Simables/INISCarControl.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Main/EventSequencer.h"

// PC: ENISCarPitch 0x6394D0, ENISCarPitch_MakeEvent_Callback 0x6395B0. Fired by an NIS's event sequence
// for one of its cars (the simable from the event's dynamic data): the car body pitches (INISCarControl::SetAnimPitch).
ENISCarPitch::ENISCarPitch(float pPitch, float pTime, uintptr_t phSimable)
    : Event(0x20), //
      fPitch(pPitch), //
      fTime(pTime), //
      fhSimable(phSimable) {
    if (fhSimable != 0) {
        ISimable *simable = ISimable::FindInstance(reinterpret_cast<HSIMABLE>(fhSimable));
        INISCarControl *iface;
        if (simable != nullptr && simable->QueryInterface(&iface)) {
            iface->SetAnimPitch(fPitch, fTime);
        }
    }
}

ENISCarPitch::~ENISCarPitch() {}

const char *ENISCarPitch::GetEventName() {
    return "ENISCarPitch";
}

void ENISCarPitch_MakeEvent_Callback(const void *staticData) {
    const ENISCarPitch::StaticData *data = static_cast<const ENISCarPitch::StaticData *>(staticData);
    new ENISCarPitch(data->fPitch, data->fTime, gEventDynamicData.fhSimable);
}
