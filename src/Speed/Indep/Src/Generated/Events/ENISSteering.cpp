#include "ENISSteering.hpp"
#include "Speed/Indep/Src/Interfaces/Simables/INISCarControl.h"
#include "Speed/Indep/Src/Interfaces/Simables/ISimable.h"
#include "Speed/Indep/Src/Main/EventSequencer.h"

// PC: ENISSteering 0x63A3F0, ENISSteering_MakeEvent_Callback 0x63A4D0. Fired by an NIS's event sequence
// for one of its cars (the simable from the event's dynamic data): the car's steering (INISCarControl::SetSteering, degrees as a fraction of a turn).
ENISSteering::ENISSteering(float pDegrees, float pBlendWeight, uintptr_t phSimable)
    : Event(0x20), //
      fDegrees(pDegrees), //
      fBlendWeight(pBlendWeight), //
      fhSimable(phSimable) {
    if (fhSimable != 0) {
        ISimable *simable = ISimable::FindInstance(reinterpret_cast<HSIMABLE>(fhSimable));
        INISCarControl *iface;
        if (simable != nullptr && simable->QueryInterface(&iface)) {
            iface->SetSteering(fDegrees * (1.0f / 360.0f), fBlendWeight);
        }
    }
}

ENISSteering::~ENISSteering() {}

const char *ENISSteering::GetEventName() {
    return "ENISSteering";
}

void ENISSteering_MakeEvent_Callback(const void *staticData) {
    const ENISSteering::StaticData *data = static_cast<const ENISSteering::StaticData *>(staticData);
    new ENISSteering(data->fDegrees, data->fBlendWeight, gEventDynamicData.fhSimable);
}
