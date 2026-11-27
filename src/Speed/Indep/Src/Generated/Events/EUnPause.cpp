#include "EUnPause.hpp"
#include "Speed/Indep/Src/Frontend/FEManager.hpp"
#include "Speed/Indep/Src/Frontend/FEngInterfaces/FEngInterface.hpp"
#include "Speed/Indep/Src/Gameplay/GRaceStatus.h"
#include "Speed/Indep/Src/Sim/Simulation.h"

// PC: EUnPause 0x625870, ~EUnPause 0x625880, EUnPause_MakeEvent_Callback
// 0x632190. Pops every frontend package over the game (which unpauses the
// simulation once none are left).
EUnPause::EUnPause() : Event(0x10) {}

EUnPause::~EUnPause() {
    cFEng::Get()->QueuePackagePop(0);
    FEManager::Get()->AllowControllerError(false);
    // The PC also reads the race type here when not split screen; the result
    // is unused.
}

const char *EUnPause::GetEventName() {
    return "EUnPause";
}

void EUnPause_MakeEvent_Callback(const void *staticData) {
    new EUnPause();
}
