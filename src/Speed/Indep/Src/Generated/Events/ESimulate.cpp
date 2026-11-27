#include "ESimulate.hpp"
#include "Speed/Indep/Src/Sim/Simulation.h"

// PC: ESimulate_MakeEvent_Callback 0x6317B0, ~ESimulate 0x625370.
// SimSystem schedules this event on Schedule_SimRate; running it steps the sim.

ESimulate::ESimulate(UTL::COM::IUnknown *pSim) : Event(0x10), fSim(pSim) {}

ESimulate::~ESimulate() {
    Sim::Update();
}

const char *ESimulate::GetEventName() {
    return "ESimulate";
}

void ESimulate_MakeEvent_Callback(const void *staticData) {
    new ESimulate(static_cast<const ESimulate::StaticData *>(staticData)->fSim);
}
