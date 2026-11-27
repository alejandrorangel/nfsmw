#include "ESndGameState.hpp"
#include "Speed/Indep/Src/EAXSound/EAXSoundEnums.hpp"

// PC: ESndGameState 0x6253D0, ~ESndGameState 0x6253F0,
// ESndGameState_MakeEvent_Callback 0x631810. Sets one of the game flow sound
// states the mixer reads (GameFlowSndState, full on = 0x7FFF).
ESndGameState::ESndGameState(int pState, bool pTurningOn)
    : Event(0x10), //
      fState(pState), //
      fTurningOn(pTurningOn) {}

ESndGameState::~ESndGameState() {
    GameFlowSndState[fState] = fTurningOn ? 0x7FFF : 0;
}

const char *ESndGameState::GetEventName() {
    return "ESndGameState";
}

void ESndGameState_MakeEvent_Callback(const void *staticData) {
    const ESndGameState::StaticData *data = static_cast<const ESndGameState::StaticData *>(staticData);
    new ESndGameState(data->fState, data->fTurningOn);
}
