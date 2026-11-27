#include "CareerGame.h"

#if defined(EA_PORT_NATIVE)
// PC: _CareerGame (0x988E2C) registers "CareerGame" -- BeginWorldLoad starts it for career free roam.
BIND_ACTIVITY_FACTORY(CareerGame);
#endif

Sim::IActivity *CareerGame::Construct(Sim::Param params) {
    return new CareerGame(Sim::Param(params));
}

CareerGame::CareerGame(Sim::Param params) : QuickGame(params) {}

CareerGame::~CareerGame() {}
